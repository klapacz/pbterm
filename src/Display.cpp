/*
 *  Copyright (C) 2013 Jens Thoms Toerring <jt@toerring.de>
 *
 *  This file is part of the pbterm program.
 *
 *  pbterm is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  pbterm is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with pbterm.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "Display.hpp"
#include "Messenger.hpp"
#include "Config.hpp"
#include "Logger.hpp"
#include "Utils.hpp"
#include "Defaults.hpp"
#include <algorithm>
#include <limits>

namespace
{

int
font_height_px( ifont const * font,
                int           fallback_size )
{
    if ( font && font->height > 0 )
        return font->height;

    return fallback_size;
}

int
cell_height_px( ifont const * font,
                int           fallback_size,
                int           line_spacing )
{
    return std::max( 1, font_height_px( font, fallback_size ) + line_spacing );
}

int
bottom_clip_guard_px( ifont const * font,
                      int           fallback_size )
{
    // InkView's raw text drawing is not reliably clipped by DrawString() near
    // the physical bottom edge on the device. If glyph pixels run past the
    // framebuffer, they can wrap/ghost at the top, which looks exactly like the
    // last terminal rows moved to row 0. Keep one extra cell-height of slack in
    // the advertised terminal geometry and additionally set an explicit clip
    // before drawing the grid.
    return font_height_px( font, fallback_size );
}

class ScopedClip
{
  public:
    ScopedClip( int x,
                int y,
                int w,
                int h )
    {
        GetClip( &m_x, &m_y, &m_w, &m_h );
        SetClip( x, y, w, h );
    }

    ~ScopedClip( )
    {
        SetClip( m_x, m_y, m_w, m_h );
    }

    ScopedClip( ScopedClip const & ) = delete;
    ScopedClip & operator=( ScopedClip const & ) = delete;

  private:
    int m_x = 0;
    int m_y = 0;
    int m_w = 0;
    int m_h = 0;
};

}


/******************************************
 * Constructor
 ******************************************/

Display::Display( Messenger & mess,
                  Config    & config )
    : m_font_name( config.font_name( ) )
    , m_font_size( config.default_font_size( ) )
    , m_orientation( GetOrientation( ) )
    , m_initial_orientation( m_orientation )
    , m_width( ScreenWidth( ) )
    , m_font( 0 )
    , m_lines( m_font_size, config.line_spacing( ), config.tab_width( ),
               X_MARGIN, Y_MARGIN, config.max_lines( ) )
    , m_is_output_suspended( false )
    , m_is_redraw_needed( false )
    , m_is_recording( false )
    , m_cell_width( 1 )
    , m_cell_height( 1 )
    , m_partial_update_count( 0 )
{
    OpenScreen( );

    // Disable the PocketBook system panel. Its area is included in
    // ScreenHeight() but the framebuffer wraps when we draw past the
    // panel-reserved region, which made the bottom rows ghost on top.
    SetPanelType( PANEL_DISABLED );

    if ( ! ( m_font = OpenFont( m_font_name.c_str( ), m_font_size, 1 ) ) )
    {
        config.logger( ).error( ) << "Can't open font '" << m_font_name
                                  << "' with size of " << m_font_size
                                  << std::endl;
        mess.send( message::Close( ) );
        return;
    }

    // Set orientation only after font has been set, the font is needed in
    // the rotate() method

    rotate( config.default_orientation( ) );
    update_cell_metrics( );
}


/******************************************
 * Destructor, switches back to original orientation and "frees" font
 ******************************************/

Display::~Display( )
{
    if ( m_orientation != m_initial_orientation )
        SetOrientation( m_initial_orientation );

    if ( m_font )
        CloseFont( m_font );
}

    
/******************************************
 * Redraws the display
 ******************************************/

void
Display::redraw( )
{
    // While output is suspended just record that a redraw will be necessary
    // when redrawing becomes possible again

    if ( m_is_output_suspended )
    {
        m_is_redraw_needed = true;
        return;
    }

    ClearScreen( );
    SetFont( m_font, BLACK );

    // Get the active display model to redraw itself. Once the Ghostty/TUI
    // screen path has been used, normal InkView repaint/show events must keep
    // redrawing that fixed-cell grid. Falling back to the legacy Lines renderer
    // here clears the visible terminal shortly after set_screen() paints it.

    if ( m_terminal_screen.has_screen( ) )
        draw_screen( m_terminal_screen.screen( ) );
    else
        m_lines.redraw( );

    // Draw a little triangle in the upper right hand corner while recording
    // is switched on

    if ( m_is_recording )
    {
        int size   = 20,
            offset = 10;

        int x  = m_width - offset - size,
            y1 = offset,
            y2 = offset + size;

        while ( y1 < y2 )
        {
            DrawLine( x, y1,   x, y2, BLACK );
            x++;
            DrawLine( x, y1++, x, y2--, BLACK );
            x++;
        }
    }

    SoftUpdate( );
}


/******************************************
 * Caches cell pixel metrics derived from the active font. Must be called
 * after any font or orientation change.
 ******************************************/

void
Display::update_cell_metrics( )
{
    SetFont( m_font, BLACK );
    m_cell_width  = std::max( 1, StringWidth( "M" ) );
    m_cell_height = cell_height_px( m_font, m_font_size, m_lines.line_spacing( ) );
}


/******************************************
 * Adds text to be shown on the display
 ******************************************/

void
Display::add_text( std::string const & str )
{
    // Note: need to set font here first, it may have been chenged behind our
    // back which would screw up the calculations done for the widths of the
    // newly added lines

    SetFont( m_font, BLACK );
    m_terminal_screen.clear( );
    m_lines.add( str );
    Repaint( );
}


/******************************************
 * Replaces all text shown on the display
 ******************************************/

void
Display::set_text( std::string const & str )
{
    SetFont( m_font, BLACK );
    m_terminal_screen.clear( );
    m_lines.set( str );
    Repaint( );
}


/******************************************
 * Draws a fixed-size terminal cell grid. This is the Ghostty/TUI path:
 * no wrapping, no trimming, no scrollback; each row and column maps to a
 * stable screen position so cursor-addressed apps such as tmux can repaint
 * cells in place.
 ******************************************/

void
Display::set_screen( TerminalScreen const & screen )
{
    if ( m_is_output_suspended )
    {
        m_is_redraw_needed = true;
        return;
    }

    if ( screen.global_dirty == 0 )
        return;

    bool const need_full = screen.global_dirty == 2
                        || ! m_terminal_screen.has_screen( )
                        || screen.cols != m_terminal_screen.screen( ).cols
                        || screen.rows != m_terminal_screen.screen( ).rows;

    if ( need_full )
    {
        m_terminal_screen.set_screen( screen );
        redraw( );
        return;
    }

    update_screen_partial( screen );
}


/******************************************
 * Draws the active fixed-cell terminal screen. The caller is responsible for
 * clearing the screen, setting the font, and issuing the final SoftUpdate().
 ******************************************/

void
Display::draw_screen( TerminalScreen const & screen )
{
    int const cell_width  = m_cell_width;
    int const cell_height = m_cell_height;
    int const clip_x = m_lines.x_margin( );
    int const clip_y = m_lines.y_margin( );
    int const clip_w = std::max( 0, ScreenWidth( ) - 2 * m_lines.x_margin( ) );
    int const clip_h = std::max( 0, ScreenHeight( )
                                    - 2 * m_lines.y_margin( )
                                    - bottom_clip_guard_px( m_font,
                                                            m_font_size ) );

    if ( clip_w <= 0 || clip_h <= 0 )
        return;

    ScopedClip clip( clip_x, clip_y, clip_w, clip_h );

    bool const has_cells = ! screen.cells.empty( );

    for ( std::size_t y = 0; y < screen.lines.size( ); ++y )
    {
        int const py = clip_y + static_cast< int >( y ) * cell_height;
        if ( py + cell_height > clip_y + clip_h )
            break;

        if ( has_cells && y < screen.cells.size( ) )
        {
            std::vector< TerminalCell > const & row = screen.cells[ y ];
            for ( std::size_t x_cell = 0; x_cell < row.size( ); ++x_cell )
            {
                int const px = clip_x + static_cast< int >( x_cell ) * cell_width;
                if ( px + cell_width > clip_x + clip_w )
                    break;

                TerminalCell const & cell = row[ x_cell ];
                bool const is_cursor =    screen.cursor_visible
                                      && x_cell == screen.cursor_x
                                      && y == screen.cursor_y;

                if ( cell.has_background || is_cursor )
                {
                    int const bg = is_cursor || cell.dark_background ? BLACK : LGRAY;
                    FillArea( px, py, cell_width, cell_height, bg );
                }

                if ( cell.has_text && cell.text != " " )
                {
                    int fg = BLACK;
                    if ( is_cursor || ( cell.has_background && cell.dark_background ) )
                        fg = WHITE;
                    else if ( cell.dim_foreground )
                        fg = DGRAY;

                    SetFont( m_font, fg );
                    DrawString( px, py, cell.text.c_str( ) );
                }
            }
            SetFont( m_font, BLACK );
            continue;
        }

        std::string const & line = screen.lines[ y ];
        std::size_t start = 0;
        while ( start < line.size( ) )
        {
            std::size_t end = line.find( ' ', start );
            if ( end == start )
            {
                ++start;
                continue;
            }
            if ( end == std::string::npos )
                end = line.size( );

            int const px = clip_x + static_cast< int >( start ) * cell_width;
            if ( px < clip_x + clip_w )
                DrawString( px, py, line.substr( start, end - start ).c_str( ) );

            start = end;
        }
    }

    if ( screen.cursor_visible && ! has_cells )
    {
        int const x = clip_x + screen.cursor_x * cell_width;
        int const y = clip_y + screen.cursor_y * cell_height;
        if ( x + cell_width <= clip_x + clip_w && y + cell_height <= clip_y + clip_h )
            FillArea( x, y, cell_width, cell_height, BLACK );
    }
}


/******************************************
 * Partial incremental repaint using Ghostty dirty-row information.
 * Only erases and repaints rows flagged dirty, then calls PartialUpdateBW
 * on the bounding rectangle of changed rows. Clean rows keep their existing
 * framebuffer pixels, which avoids ClearScreen and a full GC refresh.
 ******************************************/

void
Display::update_screen_partial( TerminalScreen const & screen )
{
    SetFont( m_font, BLACK );

    int const cell_w = m_cell_width;
    int const cell_h = m_cell_height;
    int const cx     = m_lines.x_margin( );
    int const cy     = m_lines.y_margin( );
    int const cw     = std::max( 0, ScreenWidth( )  - 2 * cx );
    int const ch     = std::max( 0, ScreenHeight( ) - 2 * cy
                                    - bottom_clip_guard_px( m_font,
                                                            m_font_size ) );
    if ( cw <= 0 || ch <= 0 )
        return;

    TerminalScreen const & prev = m_terminal_screen.screen( );
    std::size_t const prev_cursor_y = static_cast< std::size_t >( prev.cursor_y );
    bool const cursor_changed = prev.cursor_visible != screen.cursor_visible
                             || prev.cursor_y        != screen.cursor_y
                             || prev.cursor_x        != screen.cursor_x;

    ScopedClip scoped_clip( cx, cy, cw, ch );

    int dirty_top    = std::numeric_limits< int >::max( );
    int dirty_bottom = 0;

    std::size_t const n_rows = std::min< std::size_t >(
        screen.rows, screen.dirty_rows.size( ) );

    for ( std::size_t y = 0; y < n_rows; ++y )
    {
        bool const ghostty_dirty = screen.dirty_rows[ y ];
        bool const cursor_dirty  = cursor_changed && y == prev_cursor_y;

        if ( ! ghostty_dirty && ! cursor_dirty )
            continue;

        int const py = cy + static_cast< int >( y ) * cell_h;
        if ( py >= cy + ch )
            break;

        FillArea( cx, py, cw, cell_h, WHITE );

        // For ghostty-dirty rows use the new cell data; for cursor-only rows
        // use the previously stored content (cursor highlight is cleared because
        // we draw with the updated cursor position).
        std::vector< TerminalCell > const * row_ptr = nullptr;
        if ( ghostty_dirty
             && y < screen.cells.size( )
             && ! screen.cells[ y ].empty( ) )
            row_ptr = &screen.cells[ y ];
        else if ( y < prev.cells.size( ) )
            row_ptr = &prev.cells[ y ];

        if ( row_ptr )
        {
            std::vector< TerminalCell > const & row = *row_ptr;
            for ( std::size_t x = 0; x < row.size( ); ++x )
            {
                int const px = cx + static_cast< int >( x ) * cell_w;
                if ( px >= cx + cw )
                    break;

                TerminalCell const & cell = row[ x ];
                bool const is_cursor = screen.cursor_visible
                                    && x == screen.cursor_x
                                    && y == screen.cursor_y;

                if ( cell.has_background || is_cursor )
                {
                    int const bg = ( is_cursor || cell.dark_background )
                                   ? BLACK : LGRAY;
                    FillArea( px, py, cell_w, cell_h, bg );
                }

                if ( cell.has_text && cell.text != " " )
                {
                    int fg = BLACK;
                    if ( is_cursor
                         || ( cell.has_background && cell.dark_background ) )
                        fg = WHITE;
                    else if ( cell.dim_foreground )
                        fg = DGRAY;

                    SetFont( m_font, fg );
                    DrawString( px, py, cell.text.c_str( ) );
                }
            }
            SetFont( m_font, BLACK );
        }

        dirty_top    = std::min( dirty_top,    py );
        dirty_bottom = std::max( dirty_bottom, py + cell_h );
    }

    // Merge new dirty-row data into the stored screen for future EVT_SHOW redraws.
    TerminalScreen merged = prev;
    merged.cursor_x       = screen.cursor_x;
    merged.cursor_y       = screen.cursor_y;
    merged.cursor_visible = screen.cursor_visible;
    for ( std::size_t y = 0; y < n_rows; ++y )
    {
        if ( ! screen.dirty_rows[ y ] )
            continue;
        if ( y < screen.cells.size( ) && ! screen.cells[ y ].empty( ) )
        {
            if ( y < merged.cells.size( ) ) merged.cells[ y ] = screen.cells[ y ];
            if ( y < merged.lines.size( ) ) merged.lines[ y ] = screen.lines[ y ];
        }
    }
    m_terminal_screen.set_screen( merged );

    if ( dirty_top < dirty_bottom )
    {
        if ( ++m_partial_update_count >= 30 )
        {
            m_partial_update_count = 0;
            SoftUpdate( );
        }
        else
            PartialUpdateBW( cx, dirty_top, cw, dirty_bottom - dirty_top );
    }
}


/***************************************
 * Called when the user makes a swipe gesture to scroll up or down
 ***************************************/

void
Display::shift( int amount )
{
    m_lines.shift( amount );
    Repaint( );
}
    

/***************************************
 * Called when the user asks for a different orientation
 ***************************************/

void
Display::rotate( int dir )
{
    if ( dir == m_orientation )
        return;

    SetOrientation( m_orientation = dir );

    m_width = ScreenWidth( );

    SetFont( m_font, BLACK );

    m_lines.screen_dimensions_changed( );
    update_cell_metrics( );

    Repaint( );
}


/***************************************
 * Called when the user requests a larger or smaller font size (argument
 * is the difference to the current font size)
 ***************************************/

void
Display::change_font_size( int incr )
{
    int new_font_size  = m_font_size + incr;

    // Put some rstrictions on the minimum and maximum font size

    if ( new_font_size < MIN_FONT_SIZE )
        new_font_size = MIN_FONT_SIZE;

    if ( new_font_size > MAX_FONT_SIZE )
        new_font_size = MAX_FONT_SIZE;

    if ( new_font_size == m_font_size )
        return;

    ifont *new_font = OpenFont( m_font_name.c_str( ), new_font_size, 1 );

    if ( ! new_font )
        return;

    CloseFont( m_font );

    m_font = new_font;
    m_font_size = new_font_size;

    SetFont( m_font, BLACK );
    m_lines.change_font_size( m_font_size );
    update_cell_metrics( );
    Repaint( );
}


/******************************************
 * Called when the user switches recording on or off
 ******************************************/

void
Display::recording_state_change( bool state )
{
    m_is_recording = state;
    redraw( );
}


/******************************************
 * Returns the number of fixed terminal cells that fit on the current screen
 * with the active orientation and font. This is intentionally display-owned:
 * it uses the same margins and font metrics as draw_screen().
 ******************************************/

TerminalGeometry
Display::terminal_geometry( ) const
{
    TerminalGeometry geometry;

    int const cell_width  = m_cell_width;
    int const cell_height = m_cell_height;
    int const usable_width  = std::max( 0, ScreenWidth( )  - 2 * m_lines.x_margin( ) );
    int const usable_height = std::max( 0, ScreenHeight( ) - 2 * m_lines.y_margin( )
                                           - bottom_clip_guard_px( m_font,
                                                                    m_font_size ) );

    geometry.cols = static_cast< uint16_t >(
        std::max( 20, usable_width / cell_width ) );
    geometry.rows = static_cast< uint16_t >(
        std::max( 5, usable_height / cell_height ) );
    geometry.cell_width_px  = static_cast< uint32_t >( cell_width );
    geometry.cell_height_px = static_cast< uint32_t >( cell_height );

    return geometry;
}


/******************************************
 * In some situations, e.g. when the menu or keyboard is shown, screen
 * updates need to be suspended. This happens when this method is called
 * with a 'true' value. When it's later called again with a 'false' value
 * updates get re-enabled and, if there were attempts to update in between,
 * an immediate update is triggered.
 ******************************************/

void
Display::suspend_redraws( bool stop )
{
    if (    stop != m_is_output_suspended
         && ! ( m_is_output_suspended = stop )
         && m_is_redraw_needed )
    {
        m_is_redraw_needed = false;
        Repaint( );
    }
}


/*
 * Local variables:
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
