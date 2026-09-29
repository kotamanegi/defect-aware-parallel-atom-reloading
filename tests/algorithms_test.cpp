#include "algorithms.h"
#include "grid.h"
#include "simulator.h"
#include "noise.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using Mask = unsigned;
using Choice = std::pair<Mask, Mask>;
int count(Mask m) { return __builtin_popcount(m); }
bool has(Mask m, int i) { return m & (1U << i); }
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}
std::set<int> indices(Mask m)
{
    std::set<int> result;
    for (int i = 0; m; ++i, m >>= 1)
        if (m & 1U) result.insert(i);
    return result;
}
Choice masks(const std::set<int> &R, const std::set<int> &C)
{
    Choice result{};
    for (int r : R) result.first |= 1U << r;
    for (int c : C) result.second |= 1U << c;
    return result;
}
Choice masks(const ReloadParams &s) { return masks(s.R, s.C); }

// Independent oracle: evaluate the paper's per-cell formula without area deltas or cached scores.
long double energy(const Grid &a, Choice s, double p)
{
    long double result = 0;
    for (int r = 0; r < static_cast<int>(a.size()); ++r)
        for (int c = 0; c < static_cast<int>(a[r].size()); ++c)
            if (has(s.first, r) && has(s.second, c))
                result += (1 - static_cast<long double>(p)) - a[r][c];
    return result;
}

// Reference Greedy implementation that recomputes each candidate's score from the grid.
Choice referenceGreedy(const Grid &a, int mr, int mc)
{
    const int h = a.size(), w = a[0].size();
    Choice s{};
    while (count(s.first) < mr || count(s.second) < mc) {
        bool row = count(s.second) == mc ||
            (count(s.first) < mr && count(s.first) * mc < count(s.second) * mr);
        int best = -1;
        std::pair<int, int> best_score{-1, -1};
        for (int i = 0; i < (row ? h : w); ++i) {
            if (has(row ? s.first : s.second, i)) continue;
            std::pair<int, int> score{};
            for (int j = 0; j < (row ? w : h); ++j) {
                int d = 1 - (row ? a[i][j] : a[j][i]);
                score.second += d;
                if (has(row ? s.second : s.first, j)) score.first += d;
            }
            if (score > best_score) { best = i; best_score = score; }
        }
        (row ? s.first : s.second) |= 1U << best;
    }
    Choice filtered{};
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
            if (has(s.first, r) && has(s.second, c) && a[r][c] == 0) {
                filtered.first |= 1U << r;
                filtered.second |= 1U << c;
            }
    return filtered;
}

// Generate every addition/removal/swap without reusing production candidate selection.
// Break ties in order: rows before columns, add before remove before swap, ascending index.
std::vector<Choice> neighbors(Choice s, int h, int w, int mr, int mc)
{
    std::vector<Choice> result;
    for (bool row : {true, false}) {
        const Mask axis = row ? s.first : s.second;
        const int size = row ? h : w, capacity = row ? mr : mc;
        auto add = [&](Mask changed) {
            result.push_back(row ? Choice{changed, s.second} : Choice{s.first, changed});
        };
        if (count(axis) < capacity)
            for (int i = 0; i < size; ++i)
                if (!has(axis, i)) add(axis | (1U << i));
        for (int i = 0; i < size; ++i)
            if (has(axis, i)) add(axis & ~(1U << i));
        for (int i = 0; i < size; ++i)
            if (has(axis, i))
                for (int j = 0; j < size; ++j)
                    if (!has(axis, j)) add((axis & ~(1U << i)) | (1U << j));
    }
    return result;
}
std::pair<Choice, int> referenceHill(const Grid &a, Choice s, int mr, int mc, double p)
{
    int iterations = 0;
    while (true) {
        ++iterations;
        require(iterations <= 1000, "reference hill did not converge");
        Choice best = s;
        for (Choice next : neighbors(s, a.size(), a[0].size(), mr, mc))
            if (energy(a, next, p) > energy(a, best, p)) best = next;
        if (best == s) return {s, iterations};
        s = best;
    }
}
void checkLocal(const Grid &a, Choice initial, Choice final, int mr, int mc, double p)
{
    require(count(final.first) <= mr && count(final.second) <= mc, "infeasible result");
    require(energy(a, final, p) + 1e-12L >= energy(a, initial, p), "objective decreased");
    for (auto next : neighbors(final, a.size(), a[0].size(), mr, mc))
        require(energy(a, next, p) <= energy(a, final, p) + 1e-12L, "not locally optimal");
}

void exhaustive()
{
    long long cases = 0;
    const int h = 2, w = 3;
    for (unsigned bits = 0; bits < (1U << (h * w)); ++bits) {
        Grid a(h, std::vector<int>(w));
        for (int r = 0; r < h; ++r)
            for (int c = 0; c < w; ++c) a[r][c] = has(bits, r * w + c);
        for (int mr = 0; mr <= h; ++mr)
            for (int mc = 0; mc <= w; ++mc) {
                Choice greedy = referenceGreedy(a, mr, mc);
                require(masks(greedyMethod(a, 0, mr, mc, 0.02)) == greedy,
                        "Greedy disagrees with recomputed scores/filtering");
                // Compare paths, tie-breaking, and iteration counts using exactly representable binary probabilities.
                for (double p : {0.0, 0.25, 0.5, 1.0}) {
                    for (Mask rows = 0; rows < (1U << h); ++rows) {
                        if (count(rows) > mr) continue;
                        for (Mask cols = 0; cols < (1U << w); ++cols) {
                            if (count(cols) > mc) continue;
                            Choice initial{rows, cols};
                            const auto expected = referenceHill(a, initial, mr, mc, p);
                            resetHillClimbStats();
                            const auto actual = hillClimbing(a, indices(rows), indices(cols), mr, mc, p);
                            long long iterations, runs;
                            getHillClimbStats(iterations, runs);
                            require(masks(actual.first, actual.second) == expected.first,
                                    "Hill disagrees with exhaustive best improvement");
                            require(iterations == expected.second && runs == 1, "wrong convergence count");
                            checkLocal(a, initial, expected.first, mr, mc, p);
                            require(std::abs(evaluateRC(a, actual.first, actual.second, p) -
                                             energy(a, expected.first, p)) < 1e-12L, "wrong objective");
                            ++cases;
                        }
                    }
                    auto proposed = proposedMethod(a, 0, mr, mc, p);
                    require(masks(proposed) == referenceHill(a, greedy, mr, mc, p).first,
                            "Proposed is not filtered Greedy + Hill");
                }
            }
    }
    std::cout << "Exhaustive 2x3 hill cases: " << cases << '\n';
}

void regressions()
{
    Grid full(6, std::vector<int>(4, 1));
    require(std::abs(evaluateRC(full, {0,1}, {0,1}, 0.02) + 0.08L) < 1e-12L, "penalty missing");
    require(masks(greedyMethod(full, 0, 2, 2, 0.02)) == Choice{}, "full grid filtering failed");
    require(masks(proposedMethod(full, 0, 2, 2, 0.02)) == Choice{}, "full grid must need no reload");
    full[0][0] = 0;
    auto one = proposedMethod(full, 0, 2, 2, 0.02);
    require(masks(one) == Choice{1,1}, "single defect must select only its site");
    require(std::abs(evaluateRC(full, one.R, one.C, 0.02) - 0.98L) < 1e-12L, "single-defect energy");
    Grid empty(3, std::vector<int>(3, 0));
    auto expanded = hillClimbing(empty, {0}, {0}, 2, 2, 0.02);
    require(expanded.first.size() == 2 && expanded.second.size() == 2, "add moves missing");

    // One vacancy per column: high operation failure rates should remove even nonzero rows/columns.
    Grid diagonal{{0,1,1},{1,0,1},{1,1,0}};
    auto low = proposedMethod(diagonal, 0, 3, 3, 0.02);
    auto high = proposedMethod(diagonal, 0, 3, 3, 0.75);
    require(low.R.size() == 3 && low.C.size() == 3, "low-p plan wrong");
    require(high.R.size() == 1 && high.C.size() == 1, "p_op not used for remove moves");
    require(greedyMethod({}, 0, 0, 0, 0.02).R.empty(), "empty input");
    require(proposedMethod({}, 0, 0, 0, 0.02).C.empty(), "empty proposed input");

    // Check local optimality across sizes and densities using the experiment's non-binary probabilities.
    std::mt19937 gen(384723);
    for (int test = 0; test < 300; ++test) {
        int h = 2 + gen() % 5, w = 2 + gen() % 6;
        Grid a(h, std::vector<int>(w));
        for (auto &row : a) for (int &v : row) v = gen() % 2;
        int mr = 1 + gen() % h, mc = 1 + gen() % w;
        double p = test % 2 ? 0.004 : 0.02;
        auto g = greedyMethod(a, 0, mr, mc, p);
        auto s = proposedMethod(a, 0, mr, mc, p);
        require(masks(g) == referenceGreedy(a, mr, mc), "rectangular greedy mismatch");
        checkLocal(a, masks(g), masks(s), mr, mc, p);
    }
}

void integration()
{
    // Independently reproduce the simulator's procedure (TrialNoise + Planning streams)
    // to verify that per-trial noise sequences are reproducible and that p_reload
    // is passed to the planner.
    Parameters p{3,3,3,3,0.25,0.75,100,1};
    p.trial_index = 5;
    TrialNoise noise(p);
    rng = makeTrialEngine(p, RandomStream::Planning);
    Grid a = initializeStorage(p.h, p.w);
    std::vector<double> expected;
    for (int t = 0; t < p.num_iterations; ++t) {
        const auto &step = noise.nextStep();
        applyEnvironmentNoise(a, step.idle_loss);
        auto s = proposedMethod(a, t + 1, p.m_r, p.m_c, p.p_reload);
        int discarded;
        applyReloading(a, s, discarded, step.reload_failure);
        expected.push_back(static_cast<double>(countAtoms(a)) / 9);
    }
    std::vector<double> actual;
    SimulationTrace trace;
    trace.per_iter_filling_rate = &actual;
    double average = runSimulation(p, 1, trace);
    require(actual == expected, "simulator per-trial noise not reproducible");
    double expected_average = 0;
    for (size_t i = 20; i < expected.size(); ++i) expected_average += expected[i];
    expected_average /= expected.size() - 20;
    require(std::abs(average - expected_average) < 1e-12, "simulation integration");

    // With 20 operations the averaging interval is empty; reject even when a time series is requested.
    p.num_iterations = 20;
    bool rejected = false;
    try { runSimulation(p, 1, trace); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "empty post-burn-in interval accepted");
    // The first operation uses paper rows 3,6 and columns 2,4; six operations cover every site once.
    Grid visits(6, std::vector<int>(4, 0));
    for (int t = 1; t <= 6; ++t) {
        auto s = baselineMethod(visits, t, 2, 2, 0);
        if (t == 1) require(s.R == std::set<int>{2,5} && s.C == std::set<int>{1,3},
                            "baseline one-based site conversion");
        for (int r : s.R) for (int c : s.C) ++visits[r][c];
    }
    for (const auto &row : visits) for (int n : row) require(n == 1, "baseline cycle");

    // Variable-size MIP: compare all 2x3 grids and capacities against independent exhaustive subset search.
    size_t mip_cases = 0;
    for (Mask cells = 0; cells < 64; ++cells) {
        Grid sample(2, std::vector<int>(3));
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                sample[r][c] = has(cells, 3*r+c);
        for (int mr = 0; mr <= 2; ++mr)
            for (int mc = 0; mc <= 3; ++mc)
                for (double failure : {0.0, 0.004, 0.02, 0.25, 0.75, 1.0}) {
                    long double best = 0;
                    for (Mask r = 0; r < 4; ++r)
                        for (Mask c = 0; c < 8; ++c)
                            if (count(r) <= mr && count(c) <= mc)
                                best = std::max(best, energy(sample, {r,c}, failure));
                    for (int id : {3,6}) {
                        auto s = findAlgorithmById(id)->plan(sample, 0, mr, mc, failure);
                        require(s.R.size() <= static_cast<size_t>(mr) &&
                                s.C.size() <= static_cast<size_t>(mc), "MIP capacity exceeded");
                        for (int r : s.R) require(r >= 0 && r < 2, "MIP row index");
                        for (int c : s.C) require(c >= 0 && c < 3, "MIP column index");
                        require(std::abs(energy(sample, masks(s), failure) - best) < 1e-8,
                                "MIP variable-size objective mismatch: solver=" +
                                std::to_string(id) + " cells=" + std::to_string(cells) +
                                " mr=" + std::to_string(mr) + " mc=" + std::to_string(mc) +
                                " p=" + std::to_string(failure));
                        if (cells == 63 || mr == 0 || mc == 0)
                            require(s.R.empty() && s.C.empty(), "MIP empty operation");
                        ++mip_cases;
                    }
                }
    }
    std::cout << "MIP exhaustive cases: " << mip_cases << '\n';
    for (const auto &spec : algorithms()) {
        Parameters small = makeScaledParameters(1,0.004,0.02,21,1);
        double f = runSimulation(small,spec.id);
        require(std::isfinite(f) && f >= 0 && f <= 1, "registry integration failed");
    }
}
void rowByRowRegressions()
{
    const auto *spec = findAlgorithmByName("row_by_row");
    require(spec && spec->id == 7, "row-by-row CLI registry");
    Grid a{{1,0,1,0,0}, {0,1,0,1,0}, {1,1,1,1,1}};
    const auto s = spec->plan(a, 1, 2, 2, 0.02);
    require(s.R == std::set<int>{0} && s.C == std::set<int>{1,3},
            "row-by-row must honor capacity and deterministic truncation");
    const Grid before = a;
    int discarded = -1;
    applyReloading(a, s, discarded, NoiseMask(3, std::vector<unsigned char>(5, 1)));
    require(a == before && discarded == 0,
            "failed defect-only reload must preserve all occupied sites");
    require(spec->plan(a, 3, 2, 2, 0.02).R.empty(), "full row must be skipped");
    require(spec->plan(a, 4, 2, 2, 0.02).R == std::set<int>{0},
            "empty operation must not change the cyclic schedule");
    require(spec->plan(a, 1, 0, 2, 0).C.empty() &&
            spec->plan(a, 1, 2, 0, 0).R.empty(), "zero capacity");
    bool rejected = false;
    try { spec->plan(a, 0, 2, 2, 0); }
    catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "invalid operation index accepted");

    // A complete noiseless sweep must fill every defect and touch no atom.
    Grid empty(4, std::vector<int>(5, 0));
    for (int t = 1; t <= 4; ++t) {
        const auto choice = spec->plan(empty, t, 4, 5, 0);
        applyReloading(empty, choice, discarded, NoiseMask(4, std::vector<unsigned char>(5, 0)));
        require(discarded == 0 && countAtoms(empty) == 5*t, "row-by-row sweep coverage");
    }

    // Independent site-wise reference using the same physical noise masks.
    // Choose a width below capacity so the oracle needs no selection code.
    Parameters p{3, 4, 2, 4, 0.2, 0.3, 31, 1};
    for (int trial = 0; trial < 5; ++trial) {
        p.trial_index = trial;
        TrialNoise noise(p);
        Grid reference = initializeStorage(p.h, p.w);
        std::vector<double> expected, actual;
        for (int t = 1; t <= p.num_iterations; ++t) {
            const auto &mask = noise.nextStep();
            for (int r = 0; r < p.h; ++r)
                for (int c = 0; c < p.w; ++c) {
                    if (mask.idle_loss[r][c]) reference[r][c] = 0;
                    if (r == (t-1) % p.h && !reference[r][c])
                        reference[r][c] = !mask.reload_failure[r][c];
                }
            expected.push_back(static_cast<double>(countAtoms(reference)) / (p.h*p.w));
        }
        SimulationTrace trace;
        trace.per_iter_filling_rate = &actual;
        runSimulation(p, spec->id, trace);
        require(actual == expected, "row-by-row disagrees with independent state evolution");
    }
}
} // namespace

int main()
{
    try {
        exhaustive();
        regressions();
        integration();
        rowByRowRegressions();
        std::cout << "All algorithm tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
