#if ! defined TERMINAL_SCREEN_HPP_
#define TERMINAL_SCREEN_HPP_

#include <cstdint>
#include <string>
#include <vector>

struct TerminalScreen
{
    uint16_t cols = 0;
    uint16_t rows = 0;
    uint16_t cursor_x = 0;
    uint16_t cursor_y = 0;
    bool cursor_visible = false;
    std::vector< std::string > lines;
};

#endif
