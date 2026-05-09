#if ! defined GHOSTTY_TERMINAL_STATE_HPP_
#define GHOSTTY_TERMINAL_STATE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

#include "TerminalScreen.hpp"

#define GHOSTTY_STATIC 1
#include <ghostty/vt.h>

class Logger;

class GhosttyTerminalState
{
  public:
    GhosttyTerminalState( Logger & logger,
                          uint16_t cols = 80,
                          uint16_t rows = 24,
                          std::size_t max_scrollback = 1000 );

    ~GhosttyTerminalState( );

    GhosttyTerminalState( GhosttyTerminalState const & ) = delete;
    GhosttyTerminalState & operator=( GhosttyTerminalState const & ) = delete;

    bool valid( ) const;

    void write( char const * data,
                std::size_t len );

    std::string screen_text( ) const;

    TerminalScreen screen( );

    bool resize( uint16_t cols,
                 uint16_t rows,
                 uint32_t cell_width_px = 1,
                 uint32_t cell_height_px = 1 );

    GhosttyTerminal terminal( ) const;

  private:
    Logger & m_logger;
    GhosttyTerminal m_terminal;
    GhosttyRenderState m_render_state;
};

#endif
