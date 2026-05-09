#include "GhosttyTerminalState.hpp"
#include "Logger.hpp"

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

    return 0;
}
