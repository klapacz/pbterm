#include "BluetoothKeyboard.hpp"
#include "BluetoothKeyboardDiscovery.hpp"
#include "Messenger.hpp"
#include "Message.hpp"
#include "Inkview.hpp"
#include "Logger.hpp"

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
#include <iomanip>
#include <thread>

namespace
{
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

BluetoothKeyboard::BluetoothKeyboard( Messenger & mess, Logger & logger )
    : m_mess( mess )
    , m_logger( logger )
    , m_stop( false )
    , m_shift( false )
    , m_ctrl( false )
    , m_alt( false )
    , m_altgr( false )
    , m_logged_no_keyboard( false )
    , m_logged_event_id( -1 )
{
    m_logger.info() << "Bluetooth keyboard worker starting" << std::endl;
    ensure_bluetooth_awake();
    m_thread = std::thread( &BluetoothKeyboard::run, this );
}

BluetoothKeyboard::~BluetoothKeyboard()
{
    m_logger.info() << "Bluetooth keyboard worker stopping" << std::endl;
    m_stop = true;
    if ( m_thread.joinable() )
        m_thread.join();
}

void BluetoothKeyboard::ensure_bluetooth_awake()
{
    if ( IsBluetoothEnabled() == 0 )
    {
        m_logger.info() << "Bluetooth is disabled; requesting enable" << std::endl;
        SetBluetoothOn();
    }

    if ( IsBluetoothAwake() == 0 )
    {
        m_logger.info() << "Bluetooth is asleep; requesting wakeup" << std::endl;
        BluetoothWakeUp();
    }
}

bool BluetoothKeyboard::find_keyboard( Device & out )
{
    std::ifstream in( "/proc/bus/input/devices" );
    auto devices = bluetooth_keyboard_discovery::parse_bluetooth_keyboards( in );
    if ( devices.empty() )
    {
        if ( ! m_logged_no_keyboard )
        {
            m_logger.info() << "No Bluetooth keyboard found in /proc/bus/input/devices; will retry" << std::endl;
            m_logged_no_keyboard = true;
            m_logged_event_id = -1;
        }
        return false;
    }

    out = devices.front();
    m_logged_no_keyboard = false;

    if ( out.event_id != m_logged_event_id )
    {
        m_logger.info() << "Using Bluetooth keyboard " << out.name
                        << " sysfs=" << out.sysfs
                        << " event" << out.event_id << std::endl;
        m_logged_event_id = out.event_id;
    }

    return true;
}

bool BluetoothKeyboard::ensure_device_node( Device const & device )
{
    std::string path = "/dev/input/event" + std::to_string( device.event_id );
    if ( access( path.c_str(), R_OK ) == 0 )
        return true;

    m_logger.info() << path << " is not readable by pbterm; attempting root mknod fallback" << std::endl;

    std::ifstream uevent( "/sys" + device.sysfs + "/event" + std::to_string( device.event_id ) + "/uevent" );
    std::string line, major, minor;
    while ( std::getline( uevent, line ) )
    {
        if ( line.find( "MAJOR=" ) == 0 ) major = line.substr( 6 );
        if ( line.find( "MINOR=" ) == 0 ) minor = line.substr( 6 );
    }
    if ( major.empty() || minor.empty() )
    {
        m_logger.warn() << "Could not read major/minor for " << path
                        << " from /sys" << device.sysfs << std::endl;
        return false;
    }

    if ( access( "/mnt/secure/su", X_OK ) != 0 )
    {
        m_logger.warn() << "/mnt/secure/su is not executable; cannot create readable input node" << std::endl;
        return false;
    }

    std::string rm = "/mnt/secure/su rm " + path;
    std::string mk = "/mnt/secure/su mknod -m 664 " + path + " c " + major + " " + minor;
    std::system( rm.c_str() );
    int rc = std::system( mk.c_str() );
    if ( rc != 0 )
    {
        m_logger.warn() << "Failed to create " << path << " with mknod, rc=" << rc << std::endl;
        return false;
    }

    bool readable = access( path.c_str(), R_OK ) == 0;
    m_logger.info() << "Created " << path << " readable=" << ( readable ? "yes" : "no" ) << std::endl;
    return readable;
}

void BluetoothKeyboard::run()
{
    while ( ! m_stop )
    {
        ensure_bluetooth_awake();

        Device device;
        if ( find_keyboard( device ) && ensure_device_node( device ) )
            read_device( device );

        for ( int i = 0; i < 20 && ! m_stop; ++i )
            std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    }
}

void BluetoothKeyboard::read_device( Device device )
{
    std::string path = "/dev/input/event" + std::to_string( device.event_id );
    int fd = open( path.c_str( ), O_RDONLY | O_NONBLOCK );
    if ( fd < 0 )
    {
        m_logger.warn() << "Failed to open " << path << ": " << strerror( errno ) << std::endl;
        return;
    }

    m_logger.info() << "Reading Bluetooth keyboard events from " << path << std::endl;

    input_event ev;
    while ( ! m_stop )
    {
        ssize_t n = read( fd, &ev, sizeof ev );
        if ( n == static_cast<ssize_t>( sizeof ev ) )
        {
            if ( ev.type == EV_KEY )
            {
                std::string bytes = translate_key( ev.code, ev.value != 0 );
                log_keyboard_event( ev.code, ev.value, bytes );
                send_bytes( bytes );
            }
            continue;
        }

        if ( n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR )
        {
            m_logger.warn() << "Read from " << path << " failed: " << strerror( errno ) << std::endl;
            break;
        }

        std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    }

    close( fd );
    m_logger.info() << "Stopped reading Bluetooth keyboard events from " << path << std::endl;
}

std::string BluetoothKeyboard::translate_key( unsigned short code, bool press )
{
    if ( code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT ) { m_shift = press; return {}; }
    if ( code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL ) { m_ctrl = press; return {}; }
    if ( code == KEY_LEFTALT ) { m_alt = press; return {}; }
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
    std::string out;
    if ( m_ctrl && c >= 'a' && c <= 'z' ) out = std::string( 1, c - 'a' + 1 );
    else if ( m_ctrl && c >= 'A' && c <= 'Z' ) out = std::string( 1, c - 'A' + 1 );
    else out = std::string( 1, c );
    if ( m_alt ) out.insert( out.begin(), '\x1b' );
    return out;
}

void BluetoothKeyboard::send_bytes( std::string const & bytes )
{
    if ( ! bytes.empty() )
        m_mess.send( message::Terminal_Input( bytes ) );
}

void BluetoothKeyboard::log_keyboard_event( unsigned short code, int value, std::string const & bytes )
{
    m_logger.info() << "Bluetooth key event code=" << code
                    << " value=" << value
                    << " translated_len=" << bytes.size();

    if ( ! bytes.empty() )
    {
        m_logger << " bytes=";
        for ( unsigned char c : bytes )
            m_logger << "0x" << std::hex << std::setw( 2 ) << std::setfill( '0' )
                     << static_cast<int>( c ) << std::dec << std::setfill( ' ' ) << ' ';
    }

    m_logger << std::endl;
}
