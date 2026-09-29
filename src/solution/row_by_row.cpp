#include "algorithms.h"
#include <stdexcept>

ReloadParams rowByRowMethod(const Grid &A, int t, int m_r, int m_c, double /*p_op*/)
{
    if (t < 1) throw std::invalid_argument("row-by-row requires a one-based operation index");
    ReloadParams params;
    if (A.empty() || m_r <= 0 || m_c <= 0) return params;
    const int row = (t - 1) % static_cast<int>(A.size());
    for (int col = 0; col < static_cast<int>(A[row].size()); ++col) {
        if (A[row][col] == 0) params.C.insert(col);
        if (static_cast<int>(params.C.size()) == m_c) break;
    }
    if (!params.C.empty()) params.R.insert(row);
    return params;
}
