#include "../../include/algorithms.h"
#include <vector>
#include <algorithm>

// Simple Greedy method (ablation study).
// Count vacancies independently in each row and column, then select the
// top m_r rows and m_c columns. This naive greedy method does not account
// for row/column interactions through incremental u/v updates as greedyMethod does.
ReloadParams simpleGreedyMethod(const Grid &A, int /*t*/, int m_r, int m_c, double /*p_op*/)
{
    int h = A.size();
    int w = A[0].size();
    ReloadParams params;
    std::set<int> &R = params.R;
    std::set<int> &C = params.C;

    std::vector<std::pair<int, int>> rowVac(h);  // (vacancy_count, row_idx)
    std::vector<std::pair<int, int>> colVac(w);  // (vacancy_count, col_idx)

    for (int r = 0; r < h; r++) {
        int cnt = 0;
        for (int c = 0; c < w; c++) cnt += (1 - A[r][c]);
        rowVac[r] = {cnt, r};
    }
    for (int c = 0; c < w; c++) {
        int cnt = 0;
        for (int r = 0; r < h; r++) cnt += (1 - A[r][c]);
        colVac[c] = {cnt, c};
    }

    std::stable_sort(rowVac.begin(), rowVac.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });
    std::stable_sort(colVac.begin(), colVac.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    for (int i = 0; i < m_r && i < h; i++)
        R.insert(rowVac[i].second);
    for (int j = 0; j < m_c && j < w; j++)
        C.insert(colVac[j].second);

    return params;
}
