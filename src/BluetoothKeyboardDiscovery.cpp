#include "BluetoothKeyboardDiscovery.hpp"

#include <cctype>
#include <cstdlib>

namespace bluetooth_keyboard_discovery
{
namespace
{
unsigned long long const KEYBOARD_EV_BITS = 0x120013ULL;
}

int parse_event_id( std::string const & handlers )
{
    auto pos = handlers.find( "event" );
    if ( pos == std::string::npos )
        return -1;

    pos += 5;
    auto end = pos;
    while ( end < handlers.size() && std::isdigit( static_cast<unsigned char>( handlers[end] ) ) )
        ++end;

    if ( end == pos )
        return -1;

    return std::stoi( handlers.substr( pos, end - pos ) );
}

bool has_keyboard_ev_bits( std::string const & line )
{
    auto pos = line.find( "EV=" );
    if ( pos == std::string::npos )
        return false;

    pos += 3;
    char * end = nullptr;
    unsigned long long ev_bits = std::strtoull( line.c_str() + pos, &end, 16 );
    if ( end == line.c_str() + pos )
        return false;

    return ( ev_bits & KEYBOARD_EV_BITS ) == KEYBOARD_EV_BITS;
}

std::vector<Device> parse_bluetooth_keyboards( std::istream & in )
{
    std::vector<Device> devices;
    std::string line;
    Device temp;
    bool is_bt = false;
    bool has_kbd = false;
    bool has_ev = false;

    auto flush = [&]() {
        if ( ! temp.name.empty() && is_bt && has_kbd && has_ev && temp.event_id >= 0 )
            devices.push_back( temp );

        temp = Device();
        is_bt = false;
        has_kbd = false;
        has_ev = false;
    };

    while ( std::getline( in, line ) )
    {
        if ( line.empty() )
        {
            flush();
            continue;
        }

        switch ( line[0] )
        {
            case 'I':
                is_bt = line.find( "Bus=0005" ) != std::string::npos;
                break;
            case 'N':
                temp.name = line.substr( line.find( '=' ) + 1 );
                break;
            case 'S':
                temp.sysfs = line.substr( line.find( '=' ) + 1 );
                break;
            case 'H':
            {
                std::string handlers = line.substr( line.find( '=' ) + 1 );
                has_kbd = handlers.find( "kbd" ) != std::string::npos;
                temp.event_id = parse_event_id( handlers );
                break;
            }
            case 'B':
                has_ev = has_ev || has_keyboard_ev_bits( line );
                break;
        }
    }

    flush();
    return devices;
}
}
