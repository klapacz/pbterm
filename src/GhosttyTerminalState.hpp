#if ! defined GHOSTTY_TERMINAL_STATE_HPP_
#define GHOSTTY_TERMINAL_STATE_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    void init_render_helpers( );
    void free_render_helpers( );

    Logger & m_logger;
    GhosttyTerminal m_terminal;
    GhosttyRenderState m_render_state;
    GhosttyRenderStateRowIterator m_row_iterator;
    GhosttyRenderStateRowCells m_row_cells;
    std::vector< uint32_t > m_graphemes_buf;

    bool m_have_last_cursor;
    bool m_last_cursor_visible;
    uint16_t m_last_cursor_x;
    uint16_t m_last_cursor_y;
};

#endif
