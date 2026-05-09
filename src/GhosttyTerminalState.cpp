#include "GhosttyTerminalState.hpp"
#include "Logger.hpp"

GhosttyTerminalState::GhosttyTerminalState( Logger & logger,
                                            uint16_t cols,
                                            uint16_t rows,
                                            std::size_t max_scrollback )
    : m_logger( logger )
    , m_terminal( nullptr )
{
    GhosttyTerminalOptions options = { };
    options.cols = cols;
    options.rows = rows;
    options.max_scrollback = max_scrollback;

    GhosttyResult const result = ghostty_terminal_new( nullptr,
                                                       &m_terminal,
                                                       options );
    if ( result != GHOSTTY_SUCCESS )
    {
        m_terminal = nullptr;
        m_logger.error( ) << "ghostty_terminal_new() failed: "
                          << static_cast< int >( result ) << std::endl;
    }
    else
        m_logger.info( ) << "Ghostty terminal state initialized: "
                         << cols << "x" << rows << std::endl;
}

GhosttyTerminalState::~GhosttyTerminalState( )
{
    if ( m_terminal )
        ghostty_terminal_free( m_terminal );
}

bool
GhosttyTerminalState::valid( ) const
{
    return m_terminal != nullptr;
}

void
GhosttyTerminalState::write( char const * data,
                             std::size_t len )
{
    if ( ! m_terminal || ! data || len == 0 )
        return;

    ghostty_terminal_vt_write( m_terminal,
                               reinterpret_cast< uint8_t const * >( data ),
                               len );
}

GhosttyTerminal
GhosttyTerminalState::terminal( ) const
{
    return m_terminal;
}
