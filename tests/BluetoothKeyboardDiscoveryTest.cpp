#include "BluetoothKeyboardDiscovery.hpp"

#include <cassert>
#include <sstream>
#include <string>

int main()
{
    using namespace bluetooth_keyboard_discovery;

    assert( parse_event_id( "Handlers=sysrq kbd event12 leds" ) == 12 );
    assert( parse_event_id( "Handlers=kbd leds" ) == -1 );

    assert( has_keyboard_ev_bits( "B: EV=120013" ) );
    assert( has_keyboard_ev_bits( "B: EV=00120013" ) );
    assert( has_keyboard_ev_bits( "B: EV=12001F" ) );
    assert( ! has_keyboard_ev_bits( "B: EV=3" ) );
    assert( ! has_keyboard_ev_bits( "B: KEY=120013" ) );

    std::string devices =
        "I: Bus=0003 Vendor=0001 Product=0001 Version=0001\n"
        "N: Name=\"USB keyboard should be ignored\"\n"
        "S: Sysfs=/devices/usb/input/input1\n"
        "H: Handlers=sysrq kbd event1 leds\n"
        "B: EV=120013\n"
        "\n"
        "I: Bus=0005 Vendor=04e8 Product=7021 Version=011b\n"
        "N: Name=\"Bluetooth keyboard\"\n"
        "S: Sysfs=/devices/platform/bt/input/input7\n"
        "H: Handlers=sysrq kbd event7 leds\n"
        "B: EV=00120013\n";

    std::istringstream in( devices );
    auto parsed = parse_bluetooth_keyboards( in );
    assert( parsed.size() == 1 );
    assert( parsed[0].name == "\"Bluetooth keyboard\"" );
    assert( parsed[0].sysfs == "/devices/platform/bt/input/input7" );
    assert( parsed[0].event_id == 7 );

    return 0;
}
