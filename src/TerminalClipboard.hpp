#if ! defined TERMINAL_CLIPBOARD_HPP_
#define TERMINAL_CLIPBOARD_HPP_

#include <cstddef>
#include <string>

class Logger;

class TerminalClipboard
{
  public:
    explicit TerminalClipboard( Logger & logger );

    // Scan PTY output for OSC 52 clipboard sequences. If the child queries
    // the clipboard (OSC 52 ; c ; ?), returns the terminal response bytes to
    // write back to the PTY; otherwise returns an empty string.
    std::string process_output( std::string const & bytes );

    std::string const & text( ) const;
    bool empty( ) const;

    void set_text( std::string text );

    static std::string base64_encode( std::string const & data );
    static bool base64_decode( std::string const & encoded,
                               std::string       & out );

  private:
    enum class ParseState
    {
        Ground,
        Esc,
        Osc,
        OscEsc
    };

    std::string finish_osc( );
    std::string handle_osc52( std::string const & osc );
    std::string osc52_response( ) const;
    void persist( ) const;

    Logger & m_logger;
    std::string m_text;
    ParseState m_state;
    std::string m_osc;

    static std::size_t const MAX_OSC_BYTES = 1024 * 1024;
    static std::size_t const MAX_CLIPBOARD_BYTES = 1024 * 1024;
};

#endif
