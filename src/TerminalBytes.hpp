#if ! defined TERMINAL_BYTES_HPP_
#define TERMINAL_BYTES_HPP_

#include <string>

namespace terminal_bytes
{

// Normalize bytes before feeding them into a VT parser. A bare LF moves the
// cursor down without returning to column zero; CRLF keeps following lines
// aligned at the left edge. Existing CRLF sequences are preserved.
std::string lf_to_crlf( std::string const & in );

}

#endif
