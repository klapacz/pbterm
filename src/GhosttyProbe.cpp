#include "GhosttyProbe.hpp"

#define GHOSTTY_STATIC 1
#include <ghostty/vt.h>

bool
ghostty_probe_link()
{
    GhosttyTerminal terminal = nullptr;
    GhosttyTerminalOptions options = {};
    options.cols = 80;
    options.rows = 24;
    options.max_scrollback = 100;

    const GhosttyResult result = ghostty_terminal_new(nullptr, &terminal, options);
    if ( result != GHOSTTY_SUCCESS )
        return false;

    ghostty_terminal_free( terminal );
    return true;
}
