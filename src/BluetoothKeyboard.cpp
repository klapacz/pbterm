#include "BluetoothKeyboard.hpp"
#include "Messenger.hpp"
#include "Message.hpp"
#include "Inkview.hpp"

#include <linux/input.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>

namespace
{
int parse_event_id( std::string const & handlers )
{
    auto pos = handlers.find( "event" );
    if ( pos == std::string::npos )
        return -1;
    pos += 5;
    auto end = pos;
    while ( end < handlers.size() && handlers[end] >= '0' && handlers[end] <= '9' )
        ++end;
    if ( end == pos )
        return -1;
    return std::stoi( handlers.substr( pos, end - pos ) );
}

bool has_keyboard_ev_bits( std::string const & line )
{
    return line.find( "EV=" ) != std::string::npos &&
           ( line.find( "120013" ) != std::string::npos ||
             line.find( "100013" ) != std::string::npos ||
             line.find( "1f" ) != std::string::npos );
}

std::map<int, char> const normal = {
    { KEY_1, '1' }, { KEY_2, '2' }, { KEY_3, '3' }, { KEY_4, '4' }, { KEY_5, '5' },
    { KEY_6, '6' }, { KEY_7, '7' }, { KEY_8, '8' }, { KEY_9, '9' }, { KEY_0, '0' },
    { KEY_Q, 'q' }, { KEY_W, 'w' }, { KEY_E, 'e' }, { KEY_R, 'r' }, { KEY_T, 't' },
    { KEY_Y, 'y' }, { KEY_U, 'u' }, { KEY_I, 'i' }, { KEY_O, 'o' }, { KEY_P, 'p' },
    { KEY_A, 'a' }, { KEY_S, 's' }, { KEY_D, 'd' }, { KEY_F, 'f' }, { KEY_G, 'g' },
    { KEY_H, 'h' }, { KEY_J, 'j' }, { KEY_K, 'k' }, { KEY_L, 'l' },
    { KEY_Z, 'z' }, { KEY_X, 'x' }, { KEY_C, 'c' }, { KEY_V, 'v' }, { KEY_B, 'b' },
    { KEY_N, 'n' }, { KEY_M, 'm' }, { KEY_SPACE, ' ' },
    { KEY_MINUS, '-' }, { KEY_EQUAL, '=' }, { KEY_LEFTBRACE, '[' }, { KEY_RIGHTBRACE, ']' },
    { KEY_BACKSLASH, '\\' }, { KEY_SEMICOLON, ';' }, { KEY_APOSTROPHE, '\'' },
    { KEY_GRAVE, '`' }, { KEY_COMMA, ',' }, { KEY_DOT, '.' }, { KEY_SLASH, '/' }
};

std::map<int, char> const shifted = {
    { KEY_1, '!' }, { KEY_2, '@' }, { KEY_3, '#' }, { KEY_4, '$' }, { KEY_5, '%' },
    { KEY_6, '^' }, { KEY_7, '&' }, { KEY_8, '*' }, { KEY_9, '(' }, { KEY_0, ')' },
    { KEY_Q, 'Q' }, { KEY_W, 'W' }, { KEY_E, 'E' }, { KEY_R, 'R' }, { KEY_T, 'T' },
    { KEY_Y, 'Y' }, { KEY_U, 'U' }, { KEY_I, 'I' }, { KEY_O, 'O' }, { KEY_P, 'P' },
    { KEY_A, 'A' }, { KEY_S, 'S' }, { KEY_D, 'D' }, { KEY_F, 'F' }, { KEY_G, 'G' },
    { KEY_H, 'H' }, { KEY_J, 'J' }, { KEY_K, 'K' }, { KEY_L, 'L' },
    { KEY_Z, 'Z' }, { KEY_X, 'X' }, { KEY_C, 'C' }, { KEY_V, 'V' }, { KEY_B, 'B' },
    { KEY_N, 'N' }, { KEY_M, 'M' }, { KEY_SPACE, ' ' },
    { KEY_MINUS, '_' }, { KEY_EQUAL, '+' }, { KEY_LEFTBRACE, '{' }, { KEY_RIGHTBRACE, '}' },
    { KEY_BACKSLASH, '|' }, { KEY_SEMICOLON, ':' }, { KEY_APOSTROPHE, '"' },
    { KEY_GRAVE, '~' }, { KEY_COMMA, '<' }, { KEY_DOT, '>' }, { KEY_SLASH, '?' }
};
}

BluetoothKeyboard::BluetoothKeyboard( Messenger & mess )
    : m_mess( mess ), m_stop( false ), m_shift( false ), m_ctrl( false ), m_altgr( false )
{
    Device device;
    if ( find_keyboard( device ) && ensure_device_node( device ) )
        m_thread = std::thread( &BluetoothKeyboard::run, this, device );
}

BluetoothKeyboard::~BluetoothKeyboard()
{
    m_stop = true;
    if ( m_thread.joinable() )
        m_thread.join();
}

bool BluetoothKeyboard::find_keyboard( Device & out )
{
    if ( IsBluetoothEnabled() == 0 ) SetBluetoothOn();
    if ( IsBluetoothAwake() == 0 ) BluetoothWakeUp();

    std::ifstream in( "/proc/bus/input/devices" );
    std::string line;
    Device temp;
    bool is_bt = false, has_kbd = false, has_ev = false;

    auto flush = [&]() -> bool {
        if ( ! temp.name.empty() && is_bt && has_kbd && has_ev && temp.event_id >= 0 )
        { out = temp; return true; }
        temp = Device(); is_bt = has_kbd = has_ev = false; return false;
    };

    while ( std::getline( in, line ) )
    {
        if ( line.empty() ) { if ( flush() ) return true; continue; }
        if ( line[0] == 'I' ) is_bt = line.find( "Bus=0005" ) != std::string::npos;
        else if ( line[0] == 'N' ) temp.name = line.substr( line.find( '=' ) + 1 );
        else if ( line[0] == 'S' ) temp.sysfs = line.substr( line.find( '=' ) + 1 );
        else if ( line[0] == 'H' ) { has_kbd = line.find( "kbd" ) != std::string::npos; temp.event_id = parse_event_id( line ); }
        else if ( line[0] == 'B' ) has_ev = has_ev || has_keyboard_ev_bits( line );
    }
    return flush();
}

bool BluetoothKeyboard::ensure_device_node( Device const & device )
{
    std::string path = "/dev/input/event" + std::to_string( device.event_id );
    if ( access( path.c_str(), R_OK ) == 0 )
        return true;

    std::ifstream uevent( "/sys" + device.sysfs + "/event" + std::to_string( device.event_id ) + "/uevent" );
    std::string line, major, minor;
    while ( std::getline( uevent, line ) )
    {
        if ( line.find( "MAJOR=" ) == 0 ) major = line.substr( 6 );
        if ( line.find( "MINOR=" ) == 0 ) minor = line.substr( 6 );
    }
    if ( major.empty() || minor.empty() || access( "/mnt/secure/su", R_OK ) != 0 )
        return false;

    std::string rm = "/mnt/secure/su rm " + path;
    std::string mk = "/mnt/secure/su mknod -m 664 " + path + " c " + major + " " + minor;
    std::system( rm.c_str() );
    return std::system( mk.c_str() ) == 0;
}

void BluetoothKeyboard::run( Device device )
{
    std::string path = "/dev/input/event" + std::to_string( device.event_id );
    int fd = open( path.c_str( ), O_RDONLY | O_NONBLOCK );
    if ( fd < 0 )
        return;

    input_event ev;
    while ( ! m_stop )
    {
        ssize_t n = read( fd, &ev, sizeof ev );
        if ( n == static_cast<ssize_t>( sizeof ev ) )
        {
            if ( ev.type == EV_KEY )
                send_bytes( translate_key( ev.code, ev.value != 0 ) );
            continue;
        }

        if ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR )
            break;

        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }

    close( fd );
}

std::string BluetoothKeyboard::translate_key( unsigned short code, bool press )
{
    if ( code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ) { m_shift = press; return {}; }
    if ( code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL ) { m_ctrl = press; return {}; }
    if ( code == KEY_RIGHTALT ) { m_altgr = press; return {}; }
    if ( ! press ) return {};

    switch ( code )
    {
        case KEY_ENTER: return "\r";
        case KEY_BACKSPACE: return "\x7f";
        case KEY_TAB: return "\t";
        case KEY_ESC: return "\x1b";
        case KEY_CAPSLOCK: return "\x1b";
        case KEY_UP: return "\x1b[A";
        case KEY_DOWN: return "\x1b[B";
        case KEY_RIGHT: return "\x1b[C";
        case KEY_LEFT: return "\x1b[D";
        case KEY_HOME: return "\x1b[H";
        case KEY_END: return "\x1b[F";
        case KEY_DELETE: return "\x1b[3~";
        case KEY_PAGEUP: return "\x1b[5~";
        case KEY_PAGEDOWN: return "\x1b[6~";
    }

    auto const & map = m_shift ? shifted : normal;
    auto it = map.find( code );
    if ( it == map.end() ) return {};
    char c = it->second;
    if ( m_ctrl && c >= 'a' && c <= 'z' ) return std::string( 1, c - 'a' + 1 );
    if ( m_ctrl && c >= 'A' && c <= 'Z' ) return std::string( 1, c - 'A' + 1 );
    return std::string( 1, c );
}

void BluetoothKeyboard::send_bytes( std::string const & bytes )
{
    if ( ! bytes.empty() )
        m_mess.send( message::Terminal_Input( bytes ) );
}
