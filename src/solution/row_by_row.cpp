#include "algorithms.h"
#include "planning_utils.h"
#include <stdexcept>

ReloadParams rowByRowMethod(const Grid &A, int t, int m_r, int m_c, double /*p_op*/)
{
    if (t < 1) throw std::invalid_argument("row-by-row requires a one-based operation index");
    const auto [h, w] = planning_detail::gridDimensions(A);
    ReloadParams params;
    if (h == 0 || w == 0 || m_r <= 0 || m_c <= 0) return params;
    const int row = (t - 1) % h;
    for (int col = 0; col < w; ++col) {
        if (A[row][col] == 0) params.C.insert(col);
        if (static_cast<int>(params.C.size()) == m_c) break;
    }
    if (!params.C.empty()) params.R.insert(row);
    return params;
}
