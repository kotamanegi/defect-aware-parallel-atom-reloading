#include "../include/simulator.h"
#include "../include/grid.h"
#include "../include/algorithms.h"
#include "../include/noise.h"
#include "../include/timing.h"
#include <stdexcept>

// Discarded-atom statistics, accumulated across trials.
static long long g_total_discarded = 0;
static long long g_discarded_runs = 0;

void getDiscardedStats(long long &total_discarded, long long &runs)
{
    total_discarded = g_total_discarded;
    runs = g_discarded_runs;
}

void resetDiscardedStats()
{
    g_total_discarded = 0;
    g_discarded_runs = 0;
}

// Run the simulation.
// Return the mean filling rate from operation 21 onward, excluding 20 burn-in operations.
double runSimulation(const Parameters &p, int method_type, const SimulationTrace &trace)
{
    // Resolve methods through the registry; fall back to Baseline for an unknown ID.
    const AlgorithmSpec *spec = findAlgorithmById(method_type);
    if (spec == nullptr)
        spec = findAlgorithmById(0);

    // Both modes average over a nonempty interval after burn-in.
    if (p.num_iterations <= experiment_defaults::burn_in)
        throw std::invalid_argument("iterations must exceed burn-in when averaging");
    TrialNoise noise(p);
    rng = makeTrialEngine(p, RandomStream::Planning);
    Grid A = initializeStorage(p.h, p.w);
    double total_filling_rate = 0.0;
    const int burn_in = experiment_defaults::burn_in; // Exclude the first 20 operations.
    double max_planning = 0.0;
    if (trace.per_op_planning_wall_ms) trace.per_op_planning_wall_ms->clear();
    if (trace.per_iter_filling_rate) trace.per_iter_filling_rate->clear();

    for (int t = 0; t < p.num_iterations; t++)
    {
        // Apply detected atom loss (environmental noise): (t)A -> (t)A'
        const auto &step_noise = noise.nextStep();
        applyEnvironmentNoise(A, step_noise.idle_loss);

        // Determine the reloading parameters and measure planning time.
        if (trace.before_plan) trace.before_plan();
        auto [params, plan_ms] = measureWallTime([&] {
            return spec->plan(A, t + 1, p.m_r, p.m_c, p.p_reload);
        });
        if (trace.after_plan) trace.after_plan(plan_ms);
        if (plan_ms > max_planning) max_planning = plan_ms;
        if (trace.per_op_planning_wall_ms) trace.per_op_planning_wall_ms->push_back(plan_ms);

        // Execute the reloading instruction: (t)A' -> (t+1)A
        int discarded;
        applyReloading(A, params, discarded, step_noise.reload_failure);
        g_total_discarded += discarded;

        // Filling rate immediately after reloading, matching F in the paper.
        // Skip countAtoms for iterations not needed for aggregation;
        // record planning times independently of this aggregation.
        if (t >= burn_in || trace.per_iter_filling_rate)
        {
            double rate = static_cast<double>(countAtoms(A)) / (p.h * p.w);
            if (trace.per_iter_filling_rate) trace.per_iter_filling_rate->push_back(rate);
            if (t >= burn_in) total_filling_rate += rate;
        }
        if (trace.operation_completed) trace.operation_completed(plan_ms);
    }

    g_discarded_runs++;
    if (trace.max_planning_wall_ms) *trace.max_planning_wall_ms = max_planning;
    return total_filling_rate / (p.num_iterations - burn_in);
}
