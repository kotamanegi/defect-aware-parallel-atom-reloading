#ifndef SIMULATOR_H
#define SIMULATOR_H

#include "types.h"
#include <functional>

// Output destinations for simulation traces.
// Pass non-null pointers only for required traces; none are recorded by default.
struct SimulationTrace
{
    // Maximum planning wall time per operation (ms).
    double *max_planning_wall_ms = nullptr;
    // Planning wall time for each operation (ms).
    std::vector<double> *per_op_planning_wall_ms = nullptr;
    // Filling rate immediately after reloading at every iteration, including burn-in.
    std::vector<double> *per_iter_filling_rate = nullptr;
    // Hooks for enforcing the child process's hard limit; unset in ordinary runs.
    // Call before/after hooks outside the planning-time measurement.
    std::function<void()> before_plan;
    std::function<void(double)> after_plan;
    std::function<void(double)> operation_completed;
};

// Run one simulation trial.
// method_type is an ID from the registry declared in algorithms.h;
// unknown values fall back to Baseline.
// Return the mean filling rate, excluding the first 20 operations as burn-in.
// Use trace.per_iter_filling_rate to collect filling rates for every iteration.
double runSimulation(const Parameters &p, int method_type,
                     const SimulationTrace &trace = {});

// Statistics for existing atoms discarded by overwriting during reloading (accumulated in runSimulation).
void getDiscardedStats(long long &total_discarded, long long &runs);
void resetDiscardedStats();

#endif // SIMULATOR_H
