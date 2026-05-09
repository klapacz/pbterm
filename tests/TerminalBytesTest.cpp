#include "TerminalBytes.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

void
expect_eq( char const * name,
           std::string const & actual,
           std::string const & expected )
{
    if ( actual == expected )
        return;

    std::cerr << "FAIL " << name << "\n"
              << "expected size " << expected.size( ) << ": ";
    for ( unsigned char c : expected )
        std::cerr << static_cast< int >( c ) << ' ';
    std::cerr << "\nactual size " << actual.size( ) << ": ";
    for ( unsigned char c : actual )
        std::cerr << static_cast< int >( c ) << ' ';
    std::cerr << std::endl;

    std::exit( 1 );
}

}

int
main( )
{
    expect_eq( "bare LF at line boundaries",
               terminal_bytes::lf_to_crlf( "one\ntwo\nthree" ),
               "one\r\ntwo\r\nthree" );

    expect_eq( "existing CRLF is preserved",
               terminal_bytes::lf_to_crlf( "one\r\ntwo\r\nthree" ),
               "one\r\ntwo\r\nthree" );

    expect_eq( "mixed output normalizes every bare LF, not just the first",
               terminal_bytes::lf_to_crlf( "bin\nboot\ndev\r\netc\n" ),
               "bin\r\nboot\r\ndev\r\netc\r\n" );

    expect_eq( "no newline unchanged",
               terminal_bytes::lf_to_crlf( "prompt$ pwd" ),
               "prompt$ pwd" );

    return 0;
}
