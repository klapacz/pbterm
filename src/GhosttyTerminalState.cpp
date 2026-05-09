#include "GhosttyTerminalState.hpp"
#include "Logger.hpp"

namespace
{

int
luminance( GhosttyColorRgb const & color )
{
    return ( 299 * color.r + 587 * color.g + 114 * color.b ) / 1000;
}

bool
is_non_default_background( GhosttyColorRgb const & color )
{
    // Treat near-white as the normal e-ink page background. Any darker
    // terminal background is rendered as a highlighted cell, which makes
    // selections, status bars, and TUI cursor lines visible on PocketBook.
    return luminance( color ) < 235;
}

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

    if ( ! m_render_state )
    {
        ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &screen.cols );
        ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &screen.rows );
        return screen;
    }

    // The terminal grid storage can be circular after scrolling. Reading
    // GHOSTTY_POINT_TAG_ACTIVE coordinates directly can therefore expose the
    // physical row order, which makes the bottom rows appear at the top after
    // enough output. The render state is Ghostty's viewport API; it linearizes
    // the active visible screen into top-to-bottom rows and also gives cursor
    // viewport coordinates.
    if ( ghostty_render_state_update( m_render_state, m_terminal ) != GHOSTTY_SUCCESS )
    {
        m_logger.error( ) << "ghostty_render_state_update() failed" << std::endl;
        return screen;
    }

    ghostty_render_state_get( m_render_state,
                              GHOSTTY_RENDER_STATE_DATA_COLS,
                              &screen.cols );
    ghostty_render_state_get( m_render_state,
                              GHOSTTY_RENDER_STATE_DATA_ROWS,
                              &screen.rows );
    ghostty_render_state_get( m_render_state,
                              GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                              &screen.cursor_visible );

    bool has_cursor = false;
    ghostty_render_state_get( m_render_state,
                              GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
                              &has_cursor );
    if ( has_cursor )
    {
        ghostty_render_state_get( m_render_state,
                                  GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X,
                                  &screen.cursor_x );
        ghostty_render_state_get( m_render_state,
                                  GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y,
                                  &screen.cursor_y );
    }
    else
        screen.cursor_visible = false;

    screen.lines.reserve( screen.rows );

    GhosttyRenderStateRowIterator rows = nullptr;
    GhosttyRenderStateRowCells cells = nullptr;
    if (    ghostty_render_state_row_iterator_new( nullptr, &rows ) != GHOSTTY_SUCCESS
         || ghostty_render_state_row_cells_new( nullptr, &cells ) != GHOSTTY_SUCCESS
         || ghostty_render_state_get( m_render_state,
                                      GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                      rows ) != GHOSTTY_SUCCESS )
    {
        if ( cells )
            ghostty_render_state_row_cells_free( cells );
        if ( rows )
            ghostty_render_state_row_iterator_free( rows );
        m_logger.error( ) << "ghostty render row iterator setup failed" << std::endl;
        return screen;
    }

    while ( ghostty_render_state_row_iterator_next( rows )
            && screen.lines.size( ) < screen.rows )
    {
        std::string line;
        std::vector< TerminalCell > row_cells;
        row_cells.reserve( screen.cols );

        if ( ghostty_render_state_row_get( rows,
                                           GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                           cells ) == GHOSTTY_SUCCESS )
        {
            for ( uint16_t x = 0; x < screen.cols; ++x )
            {
                TerminalCell terminal_cell;
                uint32_t graphemes_len = 0;
                uint32_t codepoint = 0;

                if ( ghostty_render_state_row_cells_select( cells, x ) == GHOSTTY_SUCCESS )
                {
                    if (    ghostty_render_state_row_cells_get( cells,
                                                                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                                                                &graphemes_len ) == GHOSTTY_SUCCESS
                         && graphemes_len > 0 )
                    {
                        // GRAPHEMES_BUF writes graphemes_len uint32_t values into
                        // the caller-provided buffer. The previous code passed a
                        // single uint32_t, which corrupts the stack for combined
                        // graphemes and can make text disappear while the cursor
                        // still renders. Keep the full buffer, but render the base
                        // codepoint for now.
                        std::vector< uint32_t > graphemes( graphemes_len );
                        if ( ghostty_render_state_row_cells_get( cells,
                                                                 GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                                                                 graphemes.data( ) ) == GHOSTTY_SUCCESS )
                            codepoint = graphemes[ 0 ];
                    }

                    if ( codepoint != 0 )
                    {
                        terminal_cell.text.clear( );
                        append_utf8( terminal_cell.text, codepoint );
                        terminal_cell.has_text = true;
                    }

                    GhosttyColorRgb bg = { };
                    if ( ghostty_render_state_row_cells_get( cells,
                                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                                                             &bg ) == GHOSTTY_SUCCESS
                         && is_non_default_background( bg ) )
                    {
                        terminal_cell.has_background = true;
                        terminal_cell.dark_background = luminance( bg ) < 128;
                    }

                    GhosttyColorRgb fg = { };
                    if ( ghostty_render_state_row_cells_get( cells,
                                                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR,
                                                             &fg ) == GHOSTTY_SUCCESS )
                        terminal_cell.dim_foreground = luminance( fg ) > 170;
                }

                line += terminal_cell.has_text ? terminal_cell.text : " ";
                row_cells.push_back( terminal_cell );
            }
        }
        while ( row_cells.size( ) < screen.cols )
        {
            line += ' ';
            row_cells.push_back( TerminalCell( ) );
        }
        screen.lines.push_back( line );
        screen.cells.push_back( row_cells );
    }

    while ( screen.lines.size( ) < screen.rows )
    {
        screen.lines.push_back( std::string( screen.cols, ' ' ) );
        screen.cells.push_back( std::vector< TerminalCell >( screen.cols ) );
    }

    ghostty_render_state_row_cells_free( cells );
    ghostty_render_state_row_iterator_free( rows );

    return screen;
}

bool
GhosttyTerminalState::resize( uint16_t cols,
                              uint16_t rows,
                              uint32_t cell_width_px,
                              uint32_t cell_height_px )
{
    if ( ! m_terminal || cols == 0 || rows == 0 )
        return false;

    GhosttyResult const result = ghostty_terminal_resize( m_terminal,
                                                          cols,
                                                          rows,
                                                          cell_width_px,
                                                          cell_height_px );
    if ( result != GHOSTTY_SUCCESS )
    {
        m_logger.error( ) << "ghostty_terminal_resize() failed: "
                          << static_cast< int >( result ) << std::endl;
        return false;
    }

    m_logger.info( ) << "Ghostty terminal resized: "
                     << cols << "x" << rows << std::endl;
    return true;
}

GhosttyTerminal
GhosttyTerminalState::terminal( ) const
{
    return m_terminal;
}
