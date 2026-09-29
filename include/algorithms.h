#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "types.h"
#include <string>
#include <utility>
#include <vector>

// t is a one-based operation index. Internal R/C indices are zero-based; add 1 for paper positions.
// m_r / m_c are preparation capacities; p_op is the reloading operation failure probability.
// Greedy / Proposed / MIP select rows and columns within these capacities.

// Baseline method (Chiu et al.).
// Uses fixed row-modulo-3 and column-modulo-2 patterns; m_r and m_c do not affect selection.
ReloadParams baselineMethod(const Grid &A, int t, int m_r, int m_c, double p_op);

// Row-by-row comparator: visit row (t-1) mod h, reload only its defects,
// up to m_c sites in ascending column order. Full rows still consume a step.
ReloadParams rowByRowMethod(const Grid &A, int t, int m_r, int m_c, double p_op);

// Proposed method.
ReloadParams proposedMethod(const Grid &A, int t, int m_r, int m_c, double p_op);

// Greedy baseline (adds rows/columns with the most vacancies, choosing the axis by the ratio).
ReloadParams greedyMethod(const Grid &A, int t, int m_r, int m_c, double p_op);

// Construct the same solution as greedyMethod and write the full u/v vectors for filtered (R,C)
// to out_u / out_v: u_r = sum_{j in C}(1-X_{r,j}), v_c = sum_{i in R}(1-X_{i,c}).
// Update unfiltered u/v incrementally in O(Hp Ws + Wp Hs), as in data.tex:679-680.
ReloadParams greedyMethodWithScores(const Grid &A, int t, int m_r, int m_c, double p_op,
                                    std::vector<int> &out_u, std::vector<int> &out_v);

// Simple Greedy: independently select rows/columns with the most vacancies (ablation study).
ReloadParams simpleGreedyMethod(const Grid &A, int t, int m_r, int m_c, double p_op);

// Paper objective: vacancies - p_op * |R| * |C| (expected increase in atom count).
long double evaluateRC(const Grid &A, const std::set<int> &R,
                       const std::set<int> &C, double p_op);

// Apply the best improving row/column addition, removal, or swap until convergence.
// t is a one-based operation index. Internal R/C indices are zero-based; add 1 for paper positions.
// m_r / m_c are capacities, not grid height/width.
// If provided, row_scores_initial / col_scores_initial contain the full u/v vectors:
// u_r = sum_{c in C}(1-X_{r,c}), v_c = sum_{r in R}(1-X_{r,c}).
// Use them directly as initial scores, avoiding O(Hs Ws) recomputation as in the paper.
// If absent or incorrectly sized, recompute from R,C in O(Hs Ws).
std::pair<std::set<int>, std::set<int>> hillClimbing(
    const Grid &A, const std::set<int> &R, const std::set<int> &C,
    int m_r, int m_c, double p_op,
    const std::vector<int> *row_scores_initial = nullptr,
    const std::vector<int> *col_scores_initial = nullptr);

// MIP methods: solve the variable-size MIP with an area penalty exactly.
// Solvers have no internal time limit. Benchmark -c enforces a cumulative budget externally:
// stop the child when total planning time exceeds c * trials * operations ms.
// SCIP and GLPK implement the same formulation; both are always built.
ReloadParams mipMethodScip(const Grid &A, int t, int m_r, int m_c, double p_op);
ReloadParams mipMethodGlpk(const Grid &A, int t, int m_r, int m_c, double p_op);

// ---------------------------------------------------------------------------
// Algorithm registry.
//
// Register or unregister methods in the table in algorithm_registry.cpp.
// Dispatch, CLI parsing, and display-name resolution all use this table.
// ---------------------------------------------------------------------------
struct AlgorithmSpec
{
    int id;                   // method_type, exposed in result JSON and the simple-run -a option.
    const char *name;         // Identifier for CLI / JSON.
    const char *display_name; // Display name.
    ReloadParams (*plan)(const Grid &A, int t, int m_r, int m_c, double p_op);

    // Statistics hooks for MIP methods only; nullptr for other methods.
    // Count branch-and-bound successes and LP fallbacks separately for each solver.
    void (*reset_mip_stats)() = nullptr;
    void (*get_mip_stats)(int &bb_ok, int &lp_fallback) = nullptr;
};

// All registered methods, sorted by ascending ID.
const std::vector<AlgorithmSpec> &algorithms();

// Return nullptr if no match is found.
const AlgorithmSpec *findAlgorithmByName(const std::string &name);
const AlgorithmSpec *findAlgorithmById(int id);

// Get/reset branch-and-bound successes and LP fallbacks, independently for each solver.
// Normally called through the statistics hooks in AlgorithmSpec.
void getMipStatsScip(int &bb_ok, int &lp_fallback);
void resetMipStatsScip();
void getMipStatsGlpk(int &bb_ok, int &lp_fallback);
void resetMipStatsGlpk();

// Get/reset hill-climbing convergence statistics (total iterations and run count).
void getHillClimbStats(long long &total_iterations, long long &runs);
void resetHillClimbStats();

#endif // ALGORITHMS_H
