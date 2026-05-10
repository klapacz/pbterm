#include "TerminalClipboard.hpp"
#include "Logger.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <fstream>

namespace
{
char const b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int
base64_value( char c )
{
    if ( c >= 'A' && c <= 'Z' ) return c - 'A';
    if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
    if ( c >= '0' && c <= '9' ) return c - '0' + 52;
    if ( c == '+' ) return 62;
    if ( c == '/' ) return 63;
    return -1;
}

bool
is_base64_space( char c )
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void
write_file( std::string const & path,
            std::string const & text,
            Logger            & logger )
{
    std::string prepared = Utils::prepare_file_creation( path );
    if ( prepared.empty( ) )
        return;

    std::ofstream out( prepared.c_str( ), std::ios::out | std::ios::binary );
    if ( ! out.good( ) )
    {
        logger.warn( ) << "Could not open clipboard export file "
                       << prepared << std::endl;
        return;
    }

    out.write( text.data( ), static_cast< std::streamsize >( text.size( ) ) );
}
}

TerminalClipboard::TerminalClipboard( Logger & logger )
    : m_logger( logger )
    , m_state( ParseState::Ground )
{
}

std::string
TerminalClipboard::process_output( std::string const & bytes )
{
    std::string response;

    for ( unsigned char byte : bytes )
    {
        char const c = static_cast< char >( byte );

        switch ( m_state )
        {
            case ParseState::Ground:
                if ( c == '\x1b' )
                    m_state = ParseState::Esc;
                break;

            case ParseState::Esc:
                if ( c == ']' )
                {
                    m_osc.clear( );
                    m_state = ParseState::Osc;
                }
                else if ( c != '\x1b' )
                    m_state = ParseState::Ground;
                break;

            case ParseState::Osc:
                if ( c == '\x07' )
                {
                    std::string r = finish_osc( );
                    if ( ! r.empty( ) )
                        response += r;
                    m_state = ParseState::Ground;
                }
                else if ( c == '\x1b' )
                    m_state = ParseState::OscEsc;
                else if ( m_osc.size( ) < MAX_OSC_BYTES )
                    m_osc += c;
                else
                {
                    m_logger.warn( ) << "Dropping oversized OSC sequence" << std::endl;
                    m_osc.clear( );
                    m_state = ParseState::Ground;
                }
                break;

            case ParseState::OscEsc:
                if ( c == '\\' )
                {
                    std::string r = finish_osc( );
                    if ( ! r.empty( ) )
                        response += r;
                    m_state = ParseState::Ground;
                }
                else
                {
                    // ESC inside OSC but not ST. Treat this as a malformed OSC
                    // and let normal parsing resume from the new byte.
                    m_osc.clear( );
                    m_state = ( c == ']' ) ? ParseState::Osc : ParseState::Ground;
                }
                break;
        }
    }

    return response;
}

std::string const &
TerminalClipboard::text( ) const
{
    return m_text;
}

bool
TerminalClipboard::empty( ) const
{
    return m_text.empty( );
}

void
TerminalClipboard::set_text( std::string text )
{
    if ( text.size( ) > MAX_CLIPBOARD_BYTES )
    {
        m_logger.warn( ) << "Truncating oversized clipboard payload from "
                         << text.size( ) << " bytes to "
                         << MAX_CLIPBOARD_BYTES << " bytes" << std::endl;
        text.resize( MAX_CLIPBOARD_BYTES );
    }

    m_text = text;
    persist( );
    m_logger.info( ) << "Terminal clipboard updated: "
                     << m_text.size( ) << " bytes" << std::endl;
}

std::string
TerminalClipboard::finish_osc( )
{
    std::string const osc = m_osc;
    m_osc.clear( );

    if ( osc.compare( 0, 3, "52;" ) == 0 )
        return handle_osc52( osc );

    return std::string( );
}

std::string
TerminalClipboard::handle_osc52( std::string const & osc )
{
    std::size_t const first = osc.find( ';' );
    if ( first == std::string::npos )
        return std::string( );

    std::size_t const second = osc.find( ';', first + 1 );
    if ( second == std::string::npos )
        return std::string( );

    std::string const selection = osc.substr( first + 1, second - first - 1 );
    std::string const payload = osc.substr( second + 1 );

    // The xterm selection field is usually "c" for CLIPBOARD, but tmux and
    // other tools may include multiple selectors. pbterm only has one terminal
    // clipboard, so accept any selector and map it to that single buffer.
    (void) selection;

    if ( payload == "?" )
    {
        m_logger.info( ) << "Terminal clipboard queried via OSC 52" << std::endl;
        return osc52_response( );
    }

    std::string decoded;
    if ( ! base64_decode( payload, decoded ) )
    {
        m_logger.warn( ) << "Ignoring invalid OSC 52 clipboard payload" << std::endl;
        return std::string( );
    }

    set_text( decoded );
    return std::string( );
}

std::string
TerminalClipboard::osc52_response( ) const
{
    return std::string( "\x1b]52;c;" ) + base64_encode( m_text ) + "\x07";
}

void
TerminalClipboard::persist( ) const
{
    // There does not appear to be a public PocketBook-wide clipboard API in
    // the SDK. Export the last terminal clipboard to predictable files so it
    // can still be retrieved by scripts or other tools on-device.
    write_file( "/tmp/pbterm-clipboard.txt", m_text, m_logger );
    write_file( "/mnt/ext1/system/share/pbterm/clipboard.txt", m_text, m_logger );
}

std::string
TerminalClipboard::base64_encode( std::string const & data )
{
    std::string out;
    out.reserve( ( ( data.size( ) + 2 ) / 3 ) * 4 );

    for ( std::size_t i = 0; i < data.size( ); i += 3 )
    {
        unsigned int const b0 = static_cast< unsigned char >( data[ i ] );
        unsigned int const b1 = i + 1 < data.size( )
                              ? static_cast< unsigned char >( data[ i + 1 ] )
                              : 0;
        unsigned int const b2 = i + 2 < data.size( )
                              ? static_cast< unsigned char >( data[ i + 2 ] )
                              : 0;

        out += b64_table[ b0 >> 2 ];
        out += b64_table[ ( ( b0 & 0x03 ) << 4 ) | ( b1 >> 4 ) ];
        out += i + 1 < data.size( )
             ? b64_table[ ( ( b1 & 0x0f ) << 2 ) | ( b2 >> 6 ) ]
             : '=';
        out += i + 2 < data.size( )
             ? b64_table[ b2 & 0x3f ]
             : '=';
    }

    return out;
}

bool
TerminalClipboard::base64_decode( std::string const & encoded,
                                  std::string       & out )
{
    out.clear( );

    std::string clean;
    clean.reserve( encoded.size( ) );
    for ( char c : encoded )
    {
        if ( is_base64_space( c ) )
            continue;
        clean += c;
    }

    if ( clean.empty( ) )
        return true;

    // Some OSC 52 producers omit base64 padding. Accept that common variant
    // by restoring the implied '=' bytes before decoding.
    std::size_t const remainder = clean.size( ) % 4;
    if ( remainder == 1 )
        return false;
    if ( remainder != 0 )
        clean.append( 4 - remainder, '=' );

    out.reserve( ( clean.size( ) / 4 ) * 3 );

    bool seen_padding = false;
    for ( std::size_t i = 0; i < clean.size( ); i += 4 )
    {
        int vals[ 4 ];
        int padding = 0;

        for ( int j = 0; j < 4; ++j )
        {
            char const c = clean[ i + j ];
            if ( c == '=' )
            {
                vals[ j ] = 0;
                ++padding;
                seen_padding = true;
            }
            else
            {
                if ( seen_padding )
                    return false;
                vals[ j ] = base64_value( c );
                if ( vals[ j ] < 0 )
                    return false;
            }
        }

        if ( padding > 2 )
            return false;
        if ( padding && i + 4 != clean.size( ) )
            return false;
        if ( padding && clean[ i + 2 ] != '=' && clean[ i + 3 ] == '=' )
        {
            // one byte of padding is valid (xxx=), this branch is fine;
            // kept explicit for readability below.
        }
        if ( padding == 1 && clean[ i + 3 ] != '=' )
            return false;
        if ( padding == 2 && ( clean[ i + 2 ] != '=' || clean[ i + 3 ] != '=' ) )
            return false;

        unsigned int triple = ( vals[ 0 ] << 18 )
                            | ( vals[ 1 ] << 12 )
                            | ( vals[ 2 ] << 6 )
                            | vals[ 3 ];

        out += static_cast< char >( ( triple >> 16 ) & 0xff );
        if ( padding < 2 )
            out += static_cast< char >( ( triple >> 8 ) & 0xff );
        if ( padding < 1 )
            out += static_cast< char >( triple & 0xff );

        if ( out.size( ) > MAX_CLIPBOARD_BYTES )
            return false;
    }

    return true;
}
