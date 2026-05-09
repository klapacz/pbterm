#include "GhosttyTerminalState.hpp"
#include "Logger.hpp"

namespace
{

void
append_utf8( std::string & out,
             uint32_t      cp )
{
    if ( cp == 0 )
        return;

    if ( cp <= 0x7f )
        out += static_cast< char >( cp );
    else if ( cp <= 0x7ff )
    {
        out += static_cast< char >( 0xc0 | ( cp >> 6 ) );
        out += static_cast< char >( 0x80 | ( cp & 0x3f ) );
    }
    else if ( cp <= 0xffff )
    {
        out += static_cast< char >( 0xe0 | ( cp >> 12 ) );
        out += static_cast< char >( 0x80 | ( ( cp >> 6 ) & 0x3f ) );
        out += static_cast< char >( 0x80 | ( cp & 0x3f ) );
    }
    else if ( cp <= 0x10ffff )
    {
        out += static_cast< char >( 0xf0 | ( cp >> 18 ) );
        out += static_cast< char >( 0x80 | ( ( cp >> 12 ) & 0x3f ) );
        out += static_cast< char >( 0x80 | ( ( cp >> 6 ) & 0x3f ) );
        out += static_cast< char >( 0x80 | ( cp & 0x3f ) );
    }
}

}

GhosttyTerminalState::GhosttyTerminalState( Logger & logger,
                                            uint16_t cols,
                                            uint16_t rows,
                                            std::size_t max_scrollback )
    : m_logger( logger )
    , m_terminal( nullptr )
    , m_render_state( nullptr )
{
    GhosttyTerminalOptions options = { };
    options.cols = cols;
    options.rows = rows;
    options.max_scrollback = max_scrollback;

    GhosttyResult const result = ghostty_terminal_new( nullptr,
                                                       &m_terminal,
                                                       options );
    if ( result != GHOSTTY_SUCCESS )
    {
        m_terminal = nullptr;
        m_logger.error( ) << "ghostty_terminal_new() failed: "
                          << static_cast< int >( result ) << std::endl;
    }
    else
        m_logger.info( ) << "Ghostty terminal state initialized: "
                         << cols << "x" << rows << std::endl;

    if ( ghostty_render_state_new( nullptr, &m_render_state ) != GHOSTTY_SUCCESS )
    {
        m_render_state = nullptr;
        m_logger.error( ) << "ghostty_render_state_new() failed" << std::endl;
    }
}

GhosttyTerminalState::~GhosttyTerminalState( )
{
    if ( m_render_state )
        ghostty_render_state_free( m_render_state );

    if ( m_terminal )
        ghostty_terminal_free( m_terminal );
}

bool
GhosttyTerminalState::valid( ) const
{
    return m_terminal != nullptr;
}

void
GhosttyTerminalState::write( char const * data,
                             std::size_t len )
{
    if ( ! m_terminal || ! data || len == 0 )
        return;

    ghostty_terminal_vt_write( m_terminal,
                               reinterpret_cast< uint8_t const * >( data ),
                               len );
}

std::string
GhosttyTerminalState::screen_text( ) const
{
    std::string out;

    if ( ! m_terminal )
        return out;

    uint16_t cols = 0;
    uint16_t rows = 0;

    if (    ghostty_terminal_get( m_terminal,
                                  GHOSTTY_TERMINAL_DATA_COLS,
                                  &cols ) != GHOSTTY_SUCCESS
         || ghostty_terminal_get( m_terminal,
                                  GHOSTTY_TERMINAL_DATA_ROWS,
                                  &rows ) != GHOSTTY_SUCCESS )
        return out;

    for ( uint16_t y = 0; y < rows; ++y )
    {
        std::string line;
        std::size_t last_non_space = 0;

        for ( uint16_t x = 0; x < cols; ++x )
        {
            GhosttyPoint point = { };
            point.tag = GHOSTTY_POINT_TAG_ACTIVE;
            point.value.coordinate.x = x;
            point.value.coordinate.y = y;

            GhosttyGridRef ref = GHOSTTY_INIT_SIZED( GhosttyGridRef );
            if ( ghostty_terminal_grid_ref( m_terminal, point, &ref )
                 != GHOSTTY_SUCCESS )
            {
                line += ' ';
                continue;
            }

            GhosttyCell cell = 0;
            if ( ghostty_grid_ref_cell( &ref, &cell ) != GHOSTTY_SUCCESS )
            {
                line += ' ';
                continue;
            }

            bool has_text = false;
            uint32_t codepoint = 0;
            ghostty_cell_get( cell, GHOSTTY_CELL_DATA_HAS_TEXT, &has_text );
            ghostty_cell_get( cell, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint );

            if ( has_text && codepoint != 0 )
            {
                append_utf8( line, codepoint );
                if ( codepoint != ' ' )
                    last_non_space = line.size( );
            }
            else
                line += ' ';
        }

        line.erase( last_non_space );
        out += line;
        if ( y + 1 < rows )
            out += '\n';
    }

    return out;
}

TerminalScreen
GhosttyTerminalState::screen( )
{
    TerminalScreen screen;

    if ( ! m_terminal )
        return screen;

    ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &screen.cols );
    ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &screen.rows );
    ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_CURSOR_X, &screen.cursor_x );
    ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_CURSOR_Y, &screen.cursor_y );
    ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_CURSOR_VISIBLE, &screen.cursor_visible );

    // Keep using direct grid reads for now rather than the old text snapshot
    // path. This preserves terminal rows/columns exactly: rows are not
    // trimmed, not wrapped, and leading/trailing spaces remain real cells.
    screen.lines.reserve( screen.rows );

    for ( uint16_t y = 0; y < screen.rows; ++y )
    {
        std::string line;
        for ( uint16_t x = 0; x < screen.cols; ++x )
        {
            GhosttyPoint point = { };
            point.tag = GHOSTTY_POINT_TAG_ACTIVE;
            point.value.coordinate.x = x;
            point.value.coordinate.y = y;

            GhosttyGridRef ref = GHOSTTY_INIT_SIZED( GhosttyGridRef );
            GhosttyCell cell = 0;
            bool has_text = false;
            uint32_t codepoint = 0;

            if (    ghostty_terminal_grid_ref( m_terminal, point, &ref ) == GHOSTTY_SUCCESS
                 && ghostty_grid_ref_cell( &ref, &cell ) == GHOSTTY_SUCCESS )
            {
                ghostty_cell_get( cell, GHOSTTY_CELL_DATA_HAS_TEXT, &has_text );
                ghostty_cell_get( cell, GHOSTTY_CELL_DATA_CODEPOINT, &codepoint );
            }

            if ( has_text && codepoint != 0 )
                append_utf8( line, codepoint );
            else
                line += ' ';
        }
        screen.lines.push_back( line );
    }

    if ( m_render_state )
        ghostty_render_state_update( m_render_state, m_terminal );

    return screen;
}

GhosttyTerminal
GhosttyTerminalState::terminal( ) const
{
    return m_terminal;
}
