#ifndef BENCHMARK_RUNNER_H
#define BENCHMARK_RUNNER_H

#include "simulator.h"
#include "algorithms.h"
#include <functional>

// Send only completed trials to the parent. Counters accumulate from the start of this n.
struct TrialSummary {
    double filling_rate = 0.0;
    double runtime_ms = 0.0; // Total wall time of all planning calls in this trial (ms).
    long long hill_iterations = 0, hill_runs = 0;
    long long discarded_atoms = 0, discarded_runs = 0;
    int mip_ok = 0, mip_fallback = 0;
};

struct BenchmarkRun {
    bool timed_out = false;
    double elapsed_ms = 0.0;
    double timeout_planning_wall_ms = 0.0; // From the start notification until the parent observes termination.
    std::vector<TrialSummary> trials;
    std::vector<double> per_op_planning_wall_ms; // Only operations that completed reloading.
};

using TrialReporter = std::function<void(const TrialSummary &)>;
using PlanningWork = std::function<void(const SimulationTrace &, const TrialReporter &)>;

// Run in a POSIX child process with an OS timer for forced termination.
// Stop when cumulative planning time exceeds budget_ms.
// Arm a timer for the remaining budget before each planning call; exclude
// non-planning time. after_plan accumulates planning time and detects budget exhaustion.
// work invokes trace hooks before/after every planning call and after each completed operation.
BenchmarkRun runPlanningWorker(double budget_ms, const PlanningWork &work);
BenchmarkRun runBenchmark(const Parameters &p, const AlgorithmSpec &spec, double budget_ms);

#endif
