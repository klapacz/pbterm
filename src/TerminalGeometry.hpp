#if ! defined TERMINAL_GEOMETRY_HPP_
#define TERMINAL_GEOMETRY_HPP_

#include <cstdint>

struct TerminalGeometry
{
    uint16_t cols = 80;
    uint16_t rows = 24;
    uint32_t cell_width_px = 1;
    uint32_t cell_height_px = 1;
};

inline bool
operator==( TerminalGeometry const & lhs,
            TerminalGeometry const & rhs )
{
    return lhs.cols == rhs.cols
        && lhs.rows == rhs.rows
        && lhs.cell_width_px == rhs.cell_width_px
        && lhs.cell_height_px == rhs.cell_height_px;
}

inline bool
operator!=( TerminalGeometry const & lhs,
            TerminalGeometry const & rhs )
{
    return ! ( lhs == rhs );
}

#endif
