#if ! defined TERMINAL_GEOMETRY_HPP_
#define TERMINAL_GEOMETRY_HPP_

#include <cstdint>

struct TerminalGeometry
{
    uint16_t cols = 80;
    uint16_t rows = 24;
};

#endif
