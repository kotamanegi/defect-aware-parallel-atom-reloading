#ifndef TYPES_H
#define TYPES_H

#include "experiment_defaults.h"
#include <vector>
#include <set>
#include <random>

// Type representing the storage region's state.
using Grid = std::vector<std::vector<int>>;

// Simulation parameters.
struct Parameters
{
    int h;              // Storage region height (rows).
    int w;              // Storage region width (columns).
    int m_r;            // Preparation region rows (row selection capacity).
    int m_c;            // Preparation region columns (column selection capacity).
    double p_idle;      // Environmental noise probability (idle atom loss).
    double p_reload;    // Transport failure probability during reloading.
    int num_iterations; // Number of partial reloading operations.
    int num_trials;     // Number of simulation trials.
    std::uint64_t seed_base = experiment_defaults::seed_base;
    std::uint64_t trial_index = 0; // Fixed trial index within a condition, independent of execution order.
};

// Derive the paper's experimental grid geometry from scale factor n.
//   Storage region    : 12n x 30n
//   Preparation region: 4n x 15n
// Keep the geometry definition here so that benchmark mode (main.cpp)
// and simple-run mode (simple_run.cpp) use the same dimensions.
Parameters makeScaledParameters(int n, double p_idle, double p_reload,
                                int num_iterations, int num_trials);

// Parameters for a partial reloading instruction.
struct ReloadParams
{
    std::set<int> R;                    // Selected row indices.
    std::set<int> C;                    // Selected column indices.
    std::vector<std::pair<int, int>> F; // Reloading sites.
};

// Planning RNG; physical noise uses independent streams in TrialNoise.
extern std::mt19937 rng;

#endif // TYPES_H
