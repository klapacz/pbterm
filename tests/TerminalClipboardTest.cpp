#include "TerminalClipboard.hpp"
#include "Logger.hpp"

#include <cassert>
#include <string>

int main()
{
    Logger logger;

    assert( TerminalClipboard::base64_encode( "" ) == "" );
    assert( TerminalClipboard::base64_encode( "f" ) == "Zg==" );
    assert( TerminalClipboard::base64_encode( "fo" ) == "Zm8=" );
    assert( TerminalClipboard::base64_encode( "foo" ) == "Zm9v" );

    std::string decoded;
    assert( TerminalClipboard::base64_decode( "Zm9v", decoded ) );
    assert( decoded == "foo" );
    assert( TerminalClipboard::base64_decode( "SGVsbG8sIHBidGVybSE=", decoded ) );
    assert( decoded == "Hello, pbterm!" );
    assert( TerminalClipboard::base64_decode( "SGVsbG8sIHBidGVybSE", decoded ) );
    assert( decoded == "Hello, pbterm!" );
    assert( ! TerminalClipboard::base64_decode( "not base64", decoded ) );

    TerminalClipboard clipboard( logger );
    std::string const osc_set = std::string( "prefix\x1b]52;c;" )
                              + TerminalClipboard::base64_encode( "copied text" )
                              + "\x07suffix";
    assert( clipboard.process_output( osc_set ).empty( ) );
    assert( clipboard.text( ) == "copied text" );

    std::string const query_response = clipboard.process_output( "\x1b]52;c;?\x07" );
    assert( query_response == std::string( "\x1b]52;c;" )
                            + TerminalClipboard::base64_encode( "copied text" )
                            + "\x07" );

    TerminalClipboard split_clipboard( logger );
    assert( split_clipboard.process_output( "\x1b]52;c;U3BsaX" ).empty( ) );
    assert( split_clipboard.process_output( "Q=" "\x1b\\" ).empty( ) );
    assert( split_clipboard.text( ) == "Split" );

    return 0;
}
