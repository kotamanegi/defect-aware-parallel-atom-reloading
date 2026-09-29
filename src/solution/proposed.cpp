#include "../../include/algorithms.h"
#include "../../include/planning_utils.h"

long double evaluateRC(const Grid &A, const std::set<int> &R,
                       const std::set<int> &C, double p_op)
{
    long long defects = 0;
    for (int r : R)
        for (int c : C) defects += 1 - A[r][c];
    return defects - static_cast<long double>(p_op) * R.size() * C.size();
}

namespace {
long long hc_total_iterations = 0;
long long hc_runs = 0;

struct Move {
    bool row = true;
    int removed = -1;
    int added = -1;
    long double gain = 0;
};

// Scan each axis once for the highest unselected and lowest selected scores.
// On one axis, additions/removals share the same area change; swaps leave area unchanged.
void considerAxis(const std::vector<unsigned char> &selected,
                  const std::vector<int> &scores, int count, int capacity,
                  int other_count, double p_op, bool row, Move &best)
{
    int add = -1, remove = -1;
    for (int i = 0; i < static_cast<int>(selected.size()); ++i) {
        if (selected[i]) {
            if (remove == -1 || scores[i] < scores[remove]) remove = i;
        } else if (add == -1 || scores[i] > scores[add]) {
            add = i;
        }
    }
    const long double cost = static_cast<long double>(p_op) * other_count;
    auto consider = [&](int removed, int added, long double gain) {
        if (gain > best.gain) best = {row, removed, added, gain};
    };
    if (count < capacity && add != -1) consider(-1, add, scores[add] - cost);
    if (remove != -1) consider(remove, -1, cost - scores[remove]);
    if (add != -1 && remove != -1) consider(remove, add, scores[add] - scores[remove]);
}
} // namespace

void resetHillClimbStats() { hc_total_iterations = hc_runs = 0; }

void getHillClimbStats(long long &total_iterations, long long &runs)
{
    total_iterations = hc_total_iterations;
    runs = hc_runs;
}

std::pair<std::set<int>, std::set<int>> hillClimbing(
    const Grid &A, const std::set<int> &R, const std::set<int> &C,
    int m_r, int m_c, double p_op,
    const std::vector<int> *row_scores_initial,
    const std::vector<int> *col_scores_initial)
{
    const auto [h, w] = planning_detail::dimensions(A, m_r, m_c, p_op);
    if (R.size() > static_cast<size_t>(m_r) || C.size() > static_cast<size_t>(m_c))
        throw std::invalid_argument("hill climbing initial solution exceeds capacity");
    std::vector<unsigned char> rows(h, 0), cols(w, 0);
    for (int r : R) {
        if (r < 0 || r >= h) throw std::invalid_argument("invalid selected row");
        rows[r] = 1;
    }
    for (int c : C) {
        if (c < 0 || c >= w) throw std::invalid_argument("invalid selected column");
        cols[c] = 1;
    }
    int row_count = static_cast<int>(R.size()), col_count = static_cast<int>(C.size());
    std::vector<int> row_scores(h, 0), col_scores(w, 0);
    // Reuse Greedy's u/v to avoid O(Hs Ws) recomputation, matching the paper's incremental updates.
    // If absent or incorrectly sized, recompute from the filtered initial solution in O(Hs Ws).
    if (row_scores_initial != nullptr && col_scores_initial != nullptr &&
        row_scores_initial->size() == static_cast<size_t>(h) &&
        col_scores_initial->size() == static_cast<size_t>(w)) {
        row_scores = *row_scores_initial;
        col_scores = *col_scores_initial;
    } else {
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c) {
                const int d = 1 - A[r][c];
                if (cols[c]) row_scores[r] += d;
                if (rows[r]) col_scores[c] += d;
            }
    }

    while (true) {
        ++hc_total_iterations; // Include the final scan confirming local optimality.
        Move best;
        considerAxis(rows, row_scores, row_count, m_r, col_count, p_op, true, best);
        considerAxis(cols, col_scores, col_count, m_c, row_count, p_op, false, best);
        if (best.gain <= 0) break;

        // Update only membership flags and opposite-axis scores, avoiding set copies and tree searches.
        if (best.row) {
            if (best.removed != -1) { rows[best.removed] = 0; --row_count; }
            if (best.added != -1) { rows[best.added] = 1; ++row_count; }
            for (int c = 0; c < w; ++c) {
                if (best.removed != -1) col_scores[c] -= 1 - A[best.removed][c];
                if (best.added != -1) col_scores[c] += 1 - A[best.added][c];
            }
        } else {
            if (best.removed != -1) { cols[best.removed] = 0; --col_count; }
            if (best.added != -1) { cols[best.added] = 1; ++col_count; }
            for (int r = 0; r < h; ++r) {
                if (best.removed != -1) row_scores[r] -= 1 - A[r][best.removed];
                if (best.added != -1) row_scores[r] += 1 - A[r][best.added];
            }
        }
    }
    ++hc_runs;
    return {planning_detail::indices(rows), planning_detail::indices(cols)};
}

// Proposed uses a single Greedy initial solution and reuses Greedy's u/v
// as initial hill-climbing scores (matching incremental updates in data.tex:679-680).
ReloadParams proposedMethod(const Grid &A, int t, int m_r, int m_c, double p_op)
{
    std::vector<int> u, v;
    const auto initial = greedyMethodWithScores(A, t, m_r, m_c, p_op, u, v);
    auto [R, C] = hillClimbing(A, initial.R, initial.C, m_r, m_c, p_op, &u, &v);
    return {std::move(R), std::move(C), {}};
}
