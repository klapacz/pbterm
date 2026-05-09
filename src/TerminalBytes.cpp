#include "TerminalBytes.hpp"

namespace terminal_bytes
{

std::string
lf_to_crlf( std::string const & in )
{
    std::string out;
    out.reserve( in.size( ) + 8 );

    char prev = '\0';
    for ( std::string::const_iterator it = in.begin( );
          it != in.end( ); ++it )
    {
        if ( *it == '\n' && prev != '\r' )
            out += '\r';

        out += *it;
        prev = *it;
    }

    return out;
}

}
