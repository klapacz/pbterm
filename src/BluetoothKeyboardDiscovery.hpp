#if ! defined BLUETOOTH_KEYBOARD_DISCOVERY_HPP_
#define BLUETOOTH_KEYBOARD_DISCOVERY_HPP_

#include <istream>
#include <string>
#include <vector>

namespace bluetooth_keyboard_discovery
{
struct Device
{
    std::string name;
    std::string sysfs;
    int event_id = -1;
};

int parse_event_id( std::string const & handlers );
bool has_keyboard_ev_bits( std::string const & line );
std::vector<Device> parse_bluetooth_keyboards( std::istream & in );
}

#endif
