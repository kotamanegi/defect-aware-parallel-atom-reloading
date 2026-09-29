#ifndef PLANNING_UTILS_H
#define PLANNING_UTILS_H

#include "types.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace planning_detail {

// Input validation for Greedy and local search; zero capacity and empty grids allow a no-op.
inline std::pair<int, int> dimensions(const Grid &A, int m_r, int m_c, double p_op)
{
    const int h = static_cast<int>(A.size());
    const int w = h == 0 ? 0 : static_cast<int>(A.front().size());
    for (const auto &row : A)
        if (static_cast<int>(row.size()) != w)
            throw std::invalid_argument("planning requires a rectangular grid");
    if (m_r < 0 || m_r > h || m_c < 0 || m_c > w)
        throw std::invalid_argument("planning capacity is outside the grid");
    if (!std::isfinite(p_op) || p_op < 0 || p_op > 1)
        throw std::invalid_argument("p_op must be in [0, 1]");
    return {h, w};
}

// Ascending traversal with an end hint builds sets in O(n); use arrays for membership during search.
inline std::set<int> indices(const std::vector<unsigned char> &selected)
{
    std::set<int> result;
    for (int i = 0; i < static_cast<int>(selected.size()); ++i)
        if (selected[i]) result.insert(result.end(), i);
    return result;
}

} // namespace planning_detail
#endif
