#include "algorithms.h"
#include "planning_utils.h"
#include <stdexcept>

// Baseline method (Chiu et al.).
// Select one third of rows and half of columns using a fixed pattern (m_r and m_c are unused).
ReloadParams baselineMethod(const Grid &A, int t, int m_r, int m_c, double /*p_op*/)
{
    (void)m_r;
    (void)m_c;
    if (t < 1)
        throw std::invalid_argument("baseline requires a one-based operation index");
    const auto [h, w] = planning_detail::gridDimensions(A);
    ReloadParams params;
    if (h == 0 || w == 0)
        return params;

    // Operation t=1 corresponds to the paper's transition A^(0) -> A^(1).
    // The paper uses one-based positions i,j; internal indices r,c are i-1,j-1.
    const int phase = t - 1;
    for (int r = 0; r < h; ++r)
        if ((r + 1) % 3 == phase % 3) params.R.insert(r);
    for (int c = 0; c < w; ++c)
        if ((c + 1) % 2 == (phase / 3) % 2) params.C.insert(c);

    return params;
}
