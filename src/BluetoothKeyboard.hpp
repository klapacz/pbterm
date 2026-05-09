#if ! defined BLUETOOTH_KEYBOARD_HPP_
#define BLUETOOTH_KEYBOARD_HPP_

#include <atomic>
#include <map>
#include <string>
#include <thread>

class Messenger;

class BluetoothKeyboard
{
  public:
    explicit BluetoothKeyboard( Messenger & mess );
    ~BluetoothKeyboard();

  private:
    struct Device
    {
        std::string name;
        std::string sysfs;
        int event_id = -1;
    };

    bool find_keyboard( Device & device );
    bool ensure_device_node( Device const & device );
    void run( Device device );
    std::string translate_key( unsigned short code, bool press );
    void send_bytes( std::string const & bytes );

    Messenger & m_mess;
    std::atomic<bool> m_stop;
    std::thread m_thread;
    bool m_shift;
    bool m_ctrl;
    bool m_altgr;
};

#endif
