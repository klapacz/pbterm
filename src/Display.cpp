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
{
    OpenScreen( );

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
    m_terminal_screen.set_screen( screen );
    redraw( );
}


/******************************************
 * Draws the active fixed-cell terminal screen. The caller is responsible for
 * clearing the screen, setting the font, and issuing the final SoftUpdate().
 ******************************************/

void
Display::draw_screen( TerminalScreen const & screen )
{
    int const cell_width = StringWidth( "M" );
    int const cell_height = m_font_size + m_lines.line_spacing( );

    for ( std::size_t y = 0; y < screen.lines.size( ); ++y )
    {
        int const py = m_lines.y_margin( ) + static_cast< int >( y ) * cell_height;
        if ( py >= ScreenHeight( ) )
            break;

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

            int const px = m_lines.x_margin( ) + static_cast< int >( start ) * cell_width;
            if ( px < ScreenWidth( ) )
                DrawString( px, py, line.substr( start, end - start ).c_str( ) );

            start = end;
        }
    }

    if ( screen.cursor_visible )
    {
        int const x = m_lines.x_margin( ) + screen.cursor_x * cell_width;
        int const y = m_lines.y_margin( ) + screen.cursor_y * cell_height;
        if ( x < ScreenWidth( ) && y < ScreenHeight( ) )
            FillArea( x, y, cell_width, cell_height, BLACK );
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

    SetFont( m_font, BLACK );

    int const cell_width = std::max( 1, StringWidth( "M" ) );
    int const cell_height = std::max( 1, m_font_size + m_lines.line_spacing( ) );
    int const usable_width = std::max( 0, ScreenWidth( ) - 2 * m_lines.x_margin( ) );
    int const usable_height = std::max( 0, ScreenHeight( ) - 2 * m_lines.y_margin( ) );

    geometry.cols = static_cast< uint16_t >( std::max( 20, usable_width / cell_width ) );
    geometry.rows = static_cast< uint16_t >( std::max( 5, usable_height / cell_height ) );

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
