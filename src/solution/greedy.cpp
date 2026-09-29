#include "../../include/algorithms.h"
#include "../../include/planning_utils.h"

// Greedy from the paper: choose the axis by the ratio, then rank by (partial vacancies, total vacancies).
// Finally, remove rows and columns covering no vacancies in the same unfiltered (R,C).
// greedyMethod is the five-argument registry entry. Proposed also needs u/v,
// so it uses greedyMethodWithScores (matching incremental updates in data.tex:679-680).
ReloadParams greedyMethod(const Grid &A, int t, int m_r, int m_c, double p_op)
{
    std::vector<int> u, v;
    return greedyMethodWithScores(A, t, m_r, m_c, p_op, u, v);
}

ReloadParams greedyMethodWithScores(const Grid &A, int /*t*/, int m_r, int m_c,
                                    double p_op, std::vector<int> &out_u,
                                    std::vector<int> &out_v)
{
    const auto [h, w] = planning_detail::dimensions(A, m_r, m_c, p_op);
    if (m_r == 0 || m_c == 0) return {};

    std::vector<unsigned char> rows(h, 0), cols(w, 0);
    std::vector<int> gRow(h, 0), gCol(w, 0), u(h, 0), v(w, 0);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            const int d = 1 - A[r][c];
            gRow[r] += d;
            gCol[c] += d;
        }

    int row_count = 0, col_count = 0;
    while (row_count < m_r || col_count < m_c) {
        const bool add_row = col_count == m_c ||
            (row_count < m_r && static_cast<long long>(row_count) * m_c <
                               static_cast<long long>(col_count) * m_r);
        if (add_row) {
            int best = -1;
            long long best_score = -1;
            for (int r = 0; r < h; ++r) {
                if (rows[r]) continue;
                const long long score = static_cast<long long>(u[r]) * (w + 1LL) + gRow[r];
                if (score > best_score) {
                    best_score = score;
                    best = r;
                }
            }
            rows[best] = 1;
            ++row_count;
            for (int c = 0; c < w; ++c) v[c] += 1 - A[best][c];
        } else {
            int best = -1;
            long long best_score = -1;
            for (int c = 0; c < w; ++c) {
                if (cols[c]) continue;
                const long long score = static_cast<long long>(v[c]) * (h + 1LL) + gCol[c];
                if (score > best_score) {
                    best_score = score;
                    best = c;
                }
            }
            cols[best] = 1;
            ++col_count;
            for (int r = 0; r < h; ++r) u[r] += 1 - A[r][best];
        }
    }

    // u/v still contain scores for the unfiltered sets (I,J).
    // Identify removed rows/columns first, then subtract their contributions after filtering.
    std::vector<int> removed_rows, removed_cols;
    for (int r = 0; r < h; ++r)
        if (rows[r] && u[r] == 0) removed_rows.push_back(r);
    for (int c = 0; c < w; ++c)
        if (cols[c] && v[c] == 0) removed_cols.push_back(c);
    for (int r = 0; r < h; ++r) rows[r] = rows[r] && u[r] > 0;
    for (int c = 0; c < w; ++c) cols[c] = cols[c] && v[c] > 0;
    for (int j : removed_cols)
        for (int r = 0; r < h; ++r) u[r] -= 1 - A[r][j];
    for (int i : removed_rows)
        for (int c = 0; c < w; ++c) v[c] -= 1 - A[i][c];
    out_u = std::move(u);
    out_v = std::move(v);
    return {planning_detail::indices(rows), planning_detail::indices(cols), {}};
}
