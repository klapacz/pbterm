#include "TerminalScreenState.hpp"

#include <cassert>
#include <string>

int
main( )
{
    TerminalScreenState state;
    assert( ! state.has_screen( ) );

    TerminalScreen screen;
    screen.cols = 80;
    screen.rows = 24;
    screen.cursor_x = 4;
    screen.cursor_y = 2;
    screen.cursor_visible = true;
    screen.lines.push_back( "$ ls                                                                            " );
    screen.lines.push_back( "bin                                                                             " );

    state.set_screen( screen );

    // A later redraw must still be able to use the last Ghostty screen. This
    // catches the bug where set_screen() painted correctly, but a queued
    // InkView repaint then fell back to the legacy Lines renderer and cleared
    // the visible terminal shortly afterwards.
    assert( state.has_screen( ) );
    assert( state.screen( ).cols == 80 );
    assert( state.screen( ).rows == 24 );
    assert( state.screen( ).cursor_x == 4 );
    assert( state.screen( ).cursor_y == 2 );
    assert( state.screen( ).cursor_visible );
    assert( state.screen( ).lines.size( ) == 2 );
    assert( state.screen( ).lines[ 0 ].substr( 0, 4 ) == "$ ls" );
    assert( state.screen( ).lines[ 1 ].substr( 0, 3 ) == "bin" );

    state.clear( );
    assert( ! state.has_screen( ) );

    return 0;
}
