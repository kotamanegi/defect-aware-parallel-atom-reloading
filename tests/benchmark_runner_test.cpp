#include "benchmark_runner.h"
#include "timing.h"
#include <cerrno>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <signal.h>
#include <sys/wait.h>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void operation(const SimulationTrace &trace, Function plan)
{
    trace.before_plan();
    const auto measured = measureWallTime([&] { plan(); return 0; });
    trace.after_plan(measured.second);
    trace.operation_completed(measured.second);
}

void requireNoChildren()
{
    require(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD,
            "worker must be reaped, including timeout/error paths");
}

void testHardLimit()
{
    const auto result = runPlanningWorker(30, [](const SimulationTrace &trace, const TrialReporter &report) {
        // Even after many fast operations, a planning call that exceeds the remaining
        // budget without returning must terminate the child at the cumulative budget boundary.
        for (int i = 0; i < 100; ++i) operation(trace, [] {});
        TrialSummary trial;
        trial.filling_rate = 0.75;
        trial.runtime_ms = 1.0;
        trial.hill_iterations = 123;
        report(trial);
        operation(trace, [] {}); // Retain completed operations from the next trial as well.
        operation(trace, [] {
            // Computation with no cooperative cancellation point or result returned to the parent.
            volatile unsigned counter = 0;
            for (;;) ++counter;
        });
        report(TrialSummary{});
    });
    require(result.timed_out, "non-returning plan must time out");
    require(result.elapsed_ms < 1000, "accumulated budget must be enforced during the plan");
    require(result.timeout_planning_wall_ms >= 30, "timeout must not precede the remaining budget");
    require(result.trials.size() == 1 && result.trials[0].filling_rate == 0.75 &&
            result.trials[0].hill_iterations == 123, "completed trial must survive timeout");
    require(result.per_op_planning_wall_ms.size() == 101,
            "incomplete operation must not be recorded as completed");
    requireNoChildren();
}

void testWallTimeAndFirstOperation()
{
    const auto result = runPlanningWorker(30, [](const SimulationTrace &trace, const TrialReporter &) {
        operation(trace, [] { std::this_thread::sleep_for(std::chrono::seconds(5)); });
    });
    require(result.timed_out && result.elapsed_ms < 1000, "sleep must count as elapsed time");
    require(result.trials.empty() && result.per_op_planning_wall_ms.empty(),
            "first-operation timeout must have no completed data");
    requireNoChildren();
}

void testCumulativeBudget()
{
    // Accumulate only planning time, excluding non-planning work from the budget.
    // Four 35 ms calls total 140 ms within a 200 ms budget; intervening 120 ms waits are excluded.
    const auto within = runPlanningWorker(200, [](const SimulationTrace &trace, const TrialReporter &report) {
        for (int i = 0; i < 4; ++i) {
            operation(trace, [] { std::this_thread::sleep_for(std::chrono::milliseconds(35)); });
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }
        report(TrialSummary{});
    });
    require(!within.timed_out && within.trials.size() == 1 &&
            within.per_op_planning_wall_ms.size() == 4,
            "only planning time must count toward the cumulative budget");
    requireNoChildren();

    // Stop when cumulative time exceeds the budget, even if each call is individually below it.
    // Three 40 ms calls total 120 ms and exceed the 100 ms budget, causing a timeout.
    const auto over = runPlanningWorker(100, [](const SimulationTrace &trace, const TrialReporter &) {
        for (int i = 0; i < 3; ++i)
            operation(trace, [] { std::this_thread::sleep_for(std::chrono::milliseconds(40)); });
    });
    require(over.timed_out && over.trials.empty(),
            "cumulative plan time over budget must time out");
    require(over.per_op_planning_wall_ms.size() == 2,
            "the operation that exhausts the budget must not be recorded as completed");
    requireNoChildren();
}

void testErrors()
{
    bool thrown = false;
    try {
        runPlanningWorker(100, [](const SimulationTrace &, const TrialReporter &) {
            throw std::runtime_error("test worker failure");
        });
    } catch (const std::runtime_error &error) {
        thrown = std::string(error.what()) == "test worker failure";
    }
    require(thrown, "worker exceptions must reach parent, not be labeled timeout");
    requireNoChildren();
    thrown = false;
    try {
        runPlanningWorker(100, [](const SimulationTrace &, const TrialReporter &) { raise(SIGTERM); });
    } catch (const std::runtime_error &) { thrown = true; }
    require(thrown, "unexpected worker signal must not be labeled timeout");
    requireNoChildren();
    for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::quiet_NaN(), 1e300}) {
        thrown = false;
        try { runPlanningWorker(value, [](const SimulationTrace &, const TrialReporter &) {}); }
        catch (const std::invalid_argument &) { thrown = true; }
        require(thrown, "invalid limit must be rejected before forking");
    }
}

void testInheritedSignals()
{
    struct sigaction ignored{}, original{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    sigaction(SIGALRM, &ignored, &original);
    sigset_t alarms, old_mask;
    sigemptyset(&alarms);
    sigaddset(&alarms, SIGALRM);
    sigprocmask(SIG_BLOCK, &alarms, &old_mask);
    testWallTimeAndFirstOperation();
    struct sigaction parent_action{};
    sigaction(SIGALRM, nullptr, &parent_action);
    require(parent_action.sa_handler == SIG_IGN, "parent signal handler must remain unchanged");
    sigset_t parent_mask;
    sigprocmask(SIG_SETMASK, nullptr, &parent_mask);
    require(sigismember(&parent_mask, SIGALRM) == 1, "parent signal mask must remain unchanged");
    sigaction(SIGALRM, &original, nullptr);
    sigprocmask(SIG_SETMASK, &old_mask, nullptr);
}

void testSimulationEquivalence()
{
    for (const char *name : {"baseline", "greedy", "simple_greedy", "proposed"}) {
        const auto *spec = findAlgorithmByName(name);
        Parameters p = makeScaledParameters(2, 0.02, 0.02, 25, 2);
        resetHillClimbStats();
        resetDiscardedStats();
        std::vector<double> expected;
        for (int i = 0; i < p.num_trials; ++i) {
            p.trial_index = i;
            expected.push_back(runSimulation(p, spec->id));
        }
        long long iterations, runs, atoms, discarded_runs;
        getHillClimbStats(iterations, runs);
        getDiscardedStats(atoms, discarded_runs);
        const auto result = runBenchmark(p, *spec, 1000);
        require(!result.timed_out && result.trials.size() == 2 &&
                result.per_op_planning_wall_ms.size() == 50, "normal simulation must finish all operations");
        for (size_t i = 0; i < expected.size(); ++i) {
            require(result.trials[i].filling_rate == expected[i], "forking must preserve seeded simulation results");
            const auto begin = result.per_op_planning_wall_ms.begin() + i * p.num_iterations;
            const double plan_sum = std::accumulate(begin, begin + p.num_iterations, 0.0);
            require(result.trials[i].runtime_ms == plan_sum,
                    "trial runtime must equal its own plan times, including burn-in and excluding simulator overhead");
        }
        const auto &last = result.trials.back();
        require(last.hill_iterations == iterations && last.hill_runs == runs &&
                last.discarded_atoms == atoms && last.discarded_runs == discarded_runs,
                "child statistics must be transferred without losing or double counting trials");
        requireNoChildren();
    }
}
} // namespace

int main() try
{
    testWallTimeAndFirstOperation();
    testCumulativeBudget();
    testHardLimit();
    testErrors();
    testInheritedSignals();
    testSimulationEquivalence();
    std::cout << "Benchmark cumulative-budget tests passed.\n";
    return 0;
} catch (const std::exception &error) {
    std::cerr << "FAILED: " << error.what() << "\n";
    return 1;
}
