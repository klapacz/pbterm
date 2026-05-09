#if ! defined BLUETOOTH_KEYBOARD_HPP_
#define BLUETOOTH_KEYBOARD_HPP_

#include "BluetoothKeyboardDiscovery.hpp"

#include <atomic>
#include <map>
#include <string>
#include <thread>

class Messenger;
class Logger;

class BluetoothKeyboard
{
  public:
    BluetoothKeyboard( Messenger & mess, Logger & logger );
    ~BluetoothKeyboard();

  private:
    using Device = bluetooth_keyboard_discovery::Device;

    void ensure_bluetooth_awake();
    bool find_keyboard( Device & device );
    bool ensure_device_node( Device const & device );
    void run();
    void read_device( Device device );
    std::string translate_key( unsigned short code, bool press );
    void send_bytes( std::string const & bytes );
    void log_keyboard_event( unsigned short code, int value, std::string const & bytes );

    Messenger & m_mess;
    Logger & m_logger;
    std::atomic<bool> m_stop;
    std::thread m_thread;
    bool m_shift;
    bool m_ctrl;
    bool m_alt;
    bool m_altgr;
    bool m_logged_no_keyboard;
    int m_logged_event_id;
};

#endif
