#include "GhosttyTerminalState.hpp"
#include "Logger.hpp"

#include <cstdio>
#include <iostream>
#include <string>

namespace
{

int
fail( char const * message )
{
    std::cerr << message << std::endl;
    return 1;
}

bool
starts_with( std::string const & value,
             char const * expected )
{
    return value.rfind( expected, 0 ) == 0;
}

std::string
rtrim( std::string value )
{
    while ( ! value.empty( ) && value.back( ) == ' ' )
        value.pop_back( );
    return value;
}

}

int
main( )
{
    Logger logger;
    GhosttyTerminalState terminal( logger, 20, 5, 100 );
    if ( ! terminal.valid( ) )
        return fail( "GhosttyTerminalState is not valid" );

    char const * text = "hello\r\nworld";
    terminal.write( text, std::char_traits< char >::length( text ) );

    TerminalScreen const screen = terminal.screen( );

    if ( screen.cols != 20 )
        return fail( "unexpected screen cols" );
    if ( screen.rows != 5 )
        return fail( "unexpected screen rows" );
    if ( screen.lines.size( ) != 5 )
        return fail( "unexpected screen line count" );
    if ( screen.cells.size( ) != 5 )
        return fail( "unexpected screen cell row count" );

    if ( ! starts_with( screen.lines[ 0 ], "hello" ) )
        return fail( "first rendered row does not contain text" );
    if ( ! starts_with( screen.lines[ 1 ], "world" ) )
        return fail( "second rendered row does not contain text" );

    if ( screen.cells[ 0 ].empty( ) || ! screen.cells[ 0 ][ 0 ].has_text )
        return fail( "first cell has no text" );
    if ( screen.cells[ 0 ][ 0 ].text != "h" )
        return fail( "first cell text is wrong" );

    GhosttyTerminalState scrolled( logger, 8, 5, 100 );
    if ( ! scrolled.valid( ) )
        return fail( "scrolled GhosttyTerminalState is not valid" );

    for ( int i = 1; i <= 8; ++i )
    {
        char line[ 32 ];
        std::snprintf( line, sizeof line, "line%02d\r\n", i );
        scrolled.write( line, std::char_traits< char >::length( line ) );
    }

    TerminalScreen const scrolled_screen = scrolled.screen( );
    if ( scrolled_screen.lines.size( ) != 5 )
        return fail( "unexpected scrolled screen line count" );

    if ( rtrim( scrolled_screen.lines[ 0 ] ) != "line05" )
        return fail( "scrolled viewport row 0 is not visual top row" );
    if ( rtrim( scrolled_screen.lines[ 1 ] ) != "line06" )
        return fail( "scrolled viewport row 1 is not visual row 1" );
    if ( rtrim( scrolled_screen.lines[ 2 ] ) != "line07" )
        return fail( "scrolled viewport row 2 is not visual row 2" );
    if ( rtrim( scrolled_screen.lines[ 3 ] ) != "line08" )
        return fail( "scrolled viewport row 3 is not visual row 3" );

    return 0;
}
