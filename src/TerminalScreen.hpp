#if ! defined TERMINAL_SCREEN_HPP_
#define TERMINAL_SCREEN_HPP_

#include <cstdint>
#include <string>
#include <vector>

struct TerminalCell
{
    std::string text = " ";
    bool has_text = false;

    // E-ink friendly rendering attributes derived from Ghostty colors.
    // We intentionally keep these semantic/simple instead of preserving full
    // RGB: PocketBook InkView is monochrome-ish and TUIs mainly need visible
    // highlighted/inverted regions and a visible cursor.
    bool has_background = false;
    bool dark_background = false;
    bool dim_foreground = false;
};

struct TerminalScreen
{
    uint16_t cols = 0;
    uint16_t rows = 0;
    uint16_t cursor_x = 0;
    uint16_t cursor_y = 0;
    bool cursor_visible = false;
    std::vector< std::string > lines;
    std::vector< std::vector< TerminalCell > > cells;

    // Dirty tracking populated by GhosttyTerminalState::screen().
    // 0 = nothing changed (skip redraw entirely)
    // 1 = some rows changed (use dirty_rows for incremental repaint)
    // 2 = full redraw required (resize, alternate screen, etc.)
    int global_dirty = 2;
    std::vector< bool > dirty_rows;
};

#endif
