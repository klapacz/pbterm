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
    , m_row_iterator( nullptr )
    , m_row_cells( nullptr )
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

    init_render_helpers( );
}

GhosttyTerminalState::~GhosttyTerminalState( )
{
    free_render_helpers( );
    if ( m_terminal )
        ghostty_terminal_free( m_terminal );
}

void
GhosttyTerminalState::init_render_helpers( )
{
    m_render_state = nullptr;
    m_row_iterator = nullptr;
    m_row_cells = nullptr;

    if ( ghostty_render_state_new( nullptr, &m_render_state ) != GHOSTTY_SUCCESS )
    {
        m_render_state = nullptr;
        m_logger.error( ) << "ghostty_render_state_new() failed" << std::endl;
    }

    if ( m_render_state )
    {
        if ( ghostty_render_state_row_iterator_new( nullptr, &m_row_iterator )
             != GHOSTTY_SUCCESS )
            m_row_iterator = nullptr;
        if ( ghostty_render_state_row_cells_new( nullptr, &m_row_cells )
             != GHOSTTY_SUCCESS )
            m_row_cells = nullptr;
    }
}

void
GhosttyTerminalState::free_render_helpers( )
{
    if ( m_row_cells )
        ghostty_render_state_row_cells_free( m_row_cells );
    if ( m_row_iterator )
        ghostty_render_state_row_iterator_free( m_row_iterator );
    if ( m_render_state )
        ghostty_render_state_free( m_render_state );

    m_row_cells = nullptr;
    m_row_iterator = nullptr;
    m_render_state = nullptr;
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
    screen.global_dirty = 0;

    if ( ! m_terminal )
        return screen;

    // Defer rendering while synchronized output (mode 2026) is active.
    // Applications like Helix wrap frame updates in this mode; reading
    // the grid mid-frame would capture a partially-drawn state.
    bool sync_active = false;
    if ( ghostty_terminal_mode_get( m_terminal,
                                    GHOSTTY_MODE_SYNC_OUTPUT,
                                    &sync_active ) == GHOSTTY_SUCCESS
         && sync_active )
        return screen;

    if ( ! m_render_state )
    {
        ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_COLS, &screen.cols );
        ghostty_terminal_get( m_terminal, GHOSTTY_TERMINAL_DATA_ROWS, &screen.rows );
        screen.global_dirty = 2;
        return screen;
    }

    if ( ghostty_render_state_update( m_render_state, m_terminal ) != GHOSTTY_SUCCESS )
    {
        m_logger.error( ) << "ghostty_render_state_update() failed" << std::endl;
        return screen;
    }

    // Check global dirty state before doing any work.
    GhosttyRenderStateDirty dirty_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_get( m_render_state,
                              GHOSTTY_RENDER_STATE_DATA_DIRTY,
                              &dirty_state );
    if ( dirty_state == GHOSTTY_RENDER_STATE_DIRTY_FALSE )
        return screen;  // global_dirty == 0, caller skips redraw

    screen.global_dirty = ( dirty_state == GHOSTTY_RENDER_STATE_DIRTY_FULL ) ? 2 : 1;

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
    screen.dirty_rows.reserve( screen.rows );

    // Use persistent row iterator and cells to avoid per-frame heap allocation.
    if ( ! m_row_iterator || ! m_row_cells )
    {
        m_logger.error( ) << "ghostty render iterators not preallocated" << std::endl;
        screen.global_dirty = 0;
        return screen;
    }

    if ( ghostty_render_state_get( m_render_state,
                                   GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                   &m_row_iterator ) != GHOSTTY_SUCCESS )
    {
        m_logger.error( ) << "ghostty render row iterator setup failed" << std::endl;
        screen.global_dirty = 0;
        return screen;
    }

    bool const full = ( dirty_state == GHOSTTY_RENDER_STATE_DIRTY_FULL );

    while ( ghostty_render_state_row_iterator_next( m_row_iterator )
            && screen.lines.size( ) < screen.rows )
    {
        bool row_dirty = full;
        if ( ! full )
            ghostty_render_state_row_get( m_row_iterator,
                                          GHOSTTY_RENDER_STATE_ROW_DATA_DIRTY,
                                          &row_dirty );
        screen.dirty_rows.push_back( row_dirty );

        if ( ! row_dirty )
        {
            // Skip cell data for unchanged rows; Display keeps the previous pixels.
            screen.lines.push_back( std::string( ) );
            screen.cells.push_back( std::vector< TerminalCell >( ) );
            bool const clean = false;
            ghostty_render_state_row_set( m_row_iterator,
                                          GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY,
                                          &clean );
            continue;
        }

        std::string line;
        std::vector< TerminalCell > row_cells;
        row_cells.reserve( screen.cols );

        if ( ghostty_render_state_row_get( m_row_iterator,
                                           GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                           &m_row_cells ) == GHOSTTY_SUCCESS )
        {
            for ( uint16_t x = 0; x < screen.cols; ++x )
            {
                TerminalCell terminal_cell;
                uint32_t graphemes_len = 0;
                uint32_t codepoint = 0;

                if ( ghostty_render_state_row_cells_select( m_row_cells, x )
                     == GHOSTTY_SUCCESS )
                {
                    if (    ghostty_render_state_row_cells_get(
                                m_row_cells,
                                GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                                &graphemes_len ) == GHOSTTY_SUCCESS
                         && graphemes_len > 0 )
                    {
                        if ( m_graphemes_buf.size( ) < graphemes_len )
                            m_graphemes_buf.resize( graphemes_len );
                        if ( ghostty_render_state_row_cells_get(
                                 m_row_cells,
                                 GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                                 m_graphemes_buf.data( ) ) == GHOSTTY_SUCCESS )
                            codepoint = m_graphemes_buf[ 0 ];
                    }

                    if ( codepoint != 0 )
                    {
                        terminal_cell.text.clear( );
                        append_utf8( terminal_cell.text, codepoint );
                        terminal_cell.has_text = true;
                    }

                    GhosttyColorRgb bg = { };
                    if ( ghostty_render_state_row_cells_get(
                             m_row_cells,
                             GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR,
                             &bg ) == GHOSTTY_SUCCESS
                         && is_non_default_background( bg ) )
                    {
                        terminal_cell.has_background = true;
                        terminal_cell.dark_background = luminance( bg ) < 128;
                    }

                    GhosttyColorRgb fg = { };
                    if ( ghostty_render_state_row_cells_get(
                             m_row_cells,
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

        bool const clean = false;
        ghostty_render_state_row_set( m_row_iterator,
                                      GHOSTTY_RENDER_STATE_ROW_OPTION_DIRTY,
                                      &clean );
    }

    while ( screen.lines.size( ) < screen.rows )
    {
        screen.lines.push_back( std::string( screen.cols, ' ' ) );
        screen.cells.push_back( std::vector< TerminalCell >( screen.cols ) );
        screen.dirty_rows.push_back( full );
    }

    GhosttyRenderStateDirty const clean_state = GHOSTTY_RENDER_STATE_DIRTY_FALSE;
    ghostty_render_state_set( m_render_state,
                              GHOSTTY_RENDER_STATE_OPTION_DIRTY,
                              &clean_state );

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

    // Render-state helper objects cache Ghostty's previous grid snapshot.
    // Resizing the terminal can reallocate that grid, so drop the snapshot
    // before resize and recreate it afterwards.  This also makes the next
    // screen() call produce a fresh full-frame render instead of comparing
    // against stale row/cell objects from the old geometry.
    free_render_helpers( );
    m_graphemes_buf.clear( );

    GhosttyResult const result = ghostty_terminal_resize( m_terminal,
                                                          cols,
                                                          rows,
                                                          cell_width_px,
                                                          cell_height_px );
    init_render_helpers( );

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
