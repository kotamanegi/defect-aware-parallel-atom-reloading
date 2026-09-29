#include "../include/algorithms.h"

// Baseline method (Chiu et al.).
// Select one third of rows and half of columns using a fixed pattern (m_r and m_c are unused).
ReloadParams baselineMethod(const Grid &A, int t, int m_r, int m_c, double /*p_op*/)
{
    (void)m_r;
    (void)m_c;
    int h = A.size();
    int w = A[0].size();
    ReloadParams params;

    // Operation t=1 corresponds to the paper's transition A^(0) -> A^(1).
    // The paper uses one-based positions i,j; internal indices r,c are i-1,j-1.
    const int phase = t - 1;
    for (int r = 0; r < h; ++r)
        if ((r + 1) % 3 == phase % 3) params.R.insert(r);
    for (int c = 0; c < w; ++c)
        if ((c + 1) % 2 == (phase / 3) % 2) params.C.insert(c);

    return params;
}
