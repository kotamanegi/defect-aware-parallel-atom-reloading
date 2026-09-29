#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <string>
#include <map>
#include <filesystem>
#include <algorithm>
#include <limits>
#include <numeric>
#include "../include/types.h"
#include "../include/simulator.h"
#include "../include/algorithms.h"
#include "../include/benchmark_runner.h"
#include "../include/experiment_metadata.h"

// Join registered method names as "a, b, c" for help output.
static std::string algorithmNameList()
{
    std::string s;
    for (const auto &spec : algorithms()) {
        if (!s.empty()) s += ", ";
        s += spec.name;
    }
    return s;
}

struct AlgoResult {
    std::string environment;
    bool timed_out = false;
    size_t num_operations_done = 0;
    double elapsed_ms = 0.0;
    double timeout_planning_wall_ms = 0.0;
    int n;      // Scale factor (h=12n, w=30n).
    int h, w;
    int m_r, m_c;
    double p_idle;
    double p_reload;
    double filling_rate = 0.0;
    double filling_rate_std;                  // Sample standard deviation across trials.
    double runtime_ms;                        // Mean total planning time per trial (ms).
    double runtime_ms_std;                    // Sample standard deviation of total planning time across trials.
    double max_planning_wall_ms = 0.0;        // Maximum planning wall time over all trials and operations (ms).
    double per_op_planning_wall_ms_std = 0.0; // Sample standard deviation of planning wall time over all operations (ms).
    double avg_convergence_iterations = -1.0; // Mean hill-climbing iterations until convergence (Proposed only).
    double avg_discarded_atoms = -1.0;        // Mean number of discarded atoms per trial.
    std::vector<double> trial_filling_rates;  // Filling rate for each trial.
    std::vector<double> trial_runtimes_ms;    // Total planning time for each trial.
};

// Sample standard deviation (divide by n-1).
static double sampleStd(const std::vector<double> &v)
{
    if (v.size() < 2) return 0.0;
    double mean = 0.0;
    for (double x : v) mean += x;
    mean /= v.size();
    double sq = 0.0;
    for (double x : v) sq += (x - mean) * (x - mean);
    return std::sqrt(sq / (v.size() - 1));
}

static void writeJson(
    const std::string &filename,
    const std::string &algo,
    int seed_base, int num_trials, int num_iterations,
    double budget_ms_per_plan_call,
    const std::vector<AlgoResult> &rows)
{
    // Create the output directory (e.g. results/) if needed.
    std::filesystem::path out_path(filename);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream f(filename);
    if (!f.is_open()) {
        std::cerr << "Failed to open: " << filename << "\n";
        return;
    }
    f << std::fixed << std::setprecision(6);
    f << "{\n";
    f << "  \"algorithm\": \"" << algo << "\",\n";
    if (algo == "row_by_row") {
        f << std::setprecision(12);
        f << "  \"experiment_role\": \"additional_row_by_row_comparison\",\n";
        f << "  \"row_selection_policy\": \"cyclic row (t-1) mod h; full rows consume a step\",\n";
        f << "  \"column_selection_policy\": \"defects only, ascending column index, at most m_c\",\n";
        f << "  \"time_model\": \"one row attempt per unit-time operation; same per-step noise as main experiments\",\n";
        f << "  \"interpretation\": \"finite-horizon mean; burn-in does not establish stationarity for this comparator\",\n";
    }
    f << "  \"seed_base\": " << seed_base << ",\n";
    writeExperimentMetadata(f, num_trials);
    f << "  \"num_trials\": " << num_trials << ",\n";
    f << "  \"num_iterations\": " << num_iterations << ",\n";
    f << "  \"plan_budget_avg_ms_per_call\": " << budget_ms_per_plan_call << ",\n";
    f << "  \"plan_calls_expected\": " << (static_cast<long long>(num_trials) * num_iterations) << ",\n";
    f << "  \"plan_budget_cumulative_ms\": "
      << (budget_ms_per_plan_call * num_trials * num_iterations) << ",\n";
    f << "  \"cutoff_policy\": \"cumulative_plan_time_budget\",\n";
    f << "  \"timeout_timing_note\": \"cutoff is cumulative: the run stops when the total plan time exceeds plan_budget_avg_ms_per_call x plan_calls_expected ms; timeout_planning_wall_ms is the cumulative plan time at cutoff (or the last plan wall time when killed mid-plan)\",\n";
    f << "  \"partial_results_note\": \"trial statistics include completed trials only; planning statistics include completed operations only; timeout rows are incomplete\",\n";
    f << "  \"geometry\": \"storage h=12n, w=30n; preparation m_r=4n, m_c=15n\",\n";
    f << "  \"filling_rate_note\": \"averaged over operations "
      << experiment_defaults::burn_in + 1 << ".." << num_iterations
      << " (burn-in=" << experiment_defaults::burn_in << ")\",\n";
    f << "  \"results\": [\n";
    for (size_t i = 0; i < rows.size(); i++) {
        const auto &r = rows[i];
        f << "    {\n";
        f << "      \"environment\": \"" << r.environment << "\",\n";
        f << "      \"status\": \"" << (r.timed_out ? "timeout" : "completed") << "\",\n";
        f << "      \"num_operations_done\": " << r.num_operations_done << ",\n";
        f << "      \"elapsed_ms\": " << r.elapsed_ms << ",\n";
        if (r.timed_out) {
            f << "      \"timeout_trial\": " << r.trial_filling_rates.size() + 1 << ",\n";
            f << "      \"timeout_operation\": " << r.num_operations_done % num_iterations + 1 << ",\n";
            f << "      \"timeout_planning_wall_ms\": " << r.timeout_planning_wall_ms << ",\n";
        }
        auto metric = [&](const char *name, double value, bool available) {
            f << "      \"" << name << "\": ";
            if (available) f << value;
            else f << "null";
            f << ",\n";
        };
        const bool has_trials = !r.trial_filling_rates.empty();
        f << "      \"n\": " << r.n << ",\n";
        f << "      \"h\": " << r.h << ",\n";
        f << "      \"w\": " << r.w << ",\n";
        f << "      \"m_r\": " << r.m_r << ",\n";
        f << "      \"m_c\": " << r.m_c << ",\n";
        f << "      \"p_idle\": " << r.p_idle << ",\n";
        f << "      \"p_reload\": " << r.p_reload << ",\n";
        metric("filling_rate", r.filling_rate, has_trials);
        metric("filling_rate_percent", (r.filling_rate * 100), has_trials);
        metric("filling_rate_std", r.filling_rate_std, has_trials);
        metric("filling_rate_percent_std", (r.filling_rate_std * 100), has_trials);
        f << "      \"num_trials_done\": " << r.trial_filling_rates.size() << ",\n";
        metric("runtime_ms", r.runtime_ms, has_trials);
        metric("runtime_ms_std", r.runtime_ms_std, has_trials);
        metric("max_planning_wall_ms", r.max_planning_wall_ms, r.num_operations_done > 0);
        f << "      \"per_op_planning_wall_ms_std\": ";
        if (r.num_operations_done > 0) f << r.per_op_planning_wall_ms_std;
        else f << "null";
        if (r.avg_convergence_iterations >= 0)
            f << ",\n      \"avg_convergence_iterations\": " << r.avg_convergence_iterations;
        if (r.avg_discarded_atoms >= 0)
            f << ",\n      \"avg_discarded_atoms\": " << r.avg_discarded_atoms;
        f << ",\n      \"trial_filling_rates\": [";
        for (size_t j = 0; j < r.trial_filling_rates.size(); j++) {
            if (j) f << ", ";
            f << r.trial_filling_rates[j];
        }
        f << "],\n";
        f << "      \"trial_runtimes_ms\": [";
        for (size_t j = 0; j < r.trial_runtimes_ms.size(); j++) {
            if (j) f << ", ";
            f << r.trial_runtimes_ms[j];
        }
        f << "]\n";
        f << "    }";
        if (i + 1 < rows.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    f.close();
    std::cout << "Saved: " << filename << "\n";
}

static void printUsage(const char *prog)
{
    std::cerr << "Usage: " << prog << " [-a <algo>] [-o <output>] [-n <n_max>] [-c <budget_ms>] [-H]\n";
    std::cerr << "  -a  Algorithm: " << algorithmNameList() << ", all (default: all)\n";
    std::cerr << "  -o  Output file (default: results/results_<algo>.json)\n";
    std::cerr << "  -n  Maximum scale factor n (default: " << experiment_defaults::n_max << ", min: " << experiment_defaults::n_min << ")\n";
    std::cerr << "  -c  Per-plan-call average budget in ms. The run is stopped when the cumulative plan "
                  "time exceeds c x (trials x iterations) ms (default: 100)\n";
    std::cerr << "  -H  High-noise-only (p_idle=0.02 only; default: both noise levels)\n";
}

int main(int argc, char *argv[]) try
{
    std::string algo = "all";
    std::string output_file;
    int n_max = experiment_defaults::n_max;
    double budget_ms_per_plan_call = 100.0;
    bool high_noise_only = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if ((arg == "-a" || arg == "--algorithm") && i + 1 < argc)
            algo = argv[++i];
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc)
            output_file = argv[++i];
        else if ((arg == "-n" || arg == "--nmax") && i + 1 < argc)
            n_max = std::atoi(argv[++i]);
        else if (arg == "-c" || arg == "--cutoff") {
            if (++i == argc) throw std::invalid_argument("-c requires a duration in milliseconds");
            const std::string value = argv[i];
            size_t parsed = 0;
            budget_ms_per_plan_call = std::stod(value, &parsed);
            if (parsed != value.size() || !std::isfinite(budget_ms_per_plan_call) ||
                budget_ms_per_plan_call <= 0 ||
                budget_ms_per_plan_call >= static_cast<double>(std::numeric_limits<long long>::max()) / 1000.0)
                throw std::invalid_argument("-c must be a finite positive duration in milliseconds");
        }
        else if (arg == "-H" || arg == "--high-only")
            high_noise_only = true;
        else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
    }

    std::vector<const AlgorithmSpec *> run_algos;
    if (algo == "all") {
        for (const auto &spec : algorithms())
            run_algos.push_back(&spec);
    } else {
        const AlgorithmSpec *spec = findAlgorithmByName(algo);
        if (spec == nullptr) {
            std::cerr << "Unknown algorithm: " << algo << "\n";
            printUsage(argv[0]);
            return 1;
        }
        run_algos = {spec};
    }

    // -o applies only when a single algorithm is selected.
    if (!output_file.empty() && run_algos.size() > 1) {
        std::cerr << "Warning: -o is ignored when running multiple algorithms\n";
        output_file.clear();
    }

    const std::uint64_t seed_base = experiment_defaults::seed_base;
    const int num_iterations = experiment_defaults::iterations; // Total number of operations T.
    const int num_trials = experiment_defaults::trials;

    // Rectangular regions: storage 12n x 30n, preparation 4n x 15n (n = n_min..n_max).
    // Derive seeds deterministically for each trial (see makeTrialEngine in noise.h).
    // Seeds depend on the physical parameters, but not execution order, method, or n_max.
    const int n_min = experiment_defaults::n_min;
    std::vector<Parameters> param_sets;
    std::vector<int> param_scale;
    for (double p_idle : {experiment_defaults::low_noise, experiment_defaults::high_noise}) {
        if (high_noise_only && p_idle == 0.004) continue;
        for (int n = n_min; n <= n_max; n++) {
            Parameters p = makeScaledParameters(n, p_idle, p_idle,
                                                num_iterations, num_trials);
            param_sets.push_back(p);
            param_scale.push_back(n);
        }
    }

    std::cout << std::fixed << std::setprecision(4);

    for (const AlgorithmSpec *spec : run_algos) {
        const std::string a = spec->name;
        std::string out = output_file.empty() ? ("results/results_" + a + ".json") : output_file;

        std::cout << "\n=== " << a << " ===\n";

        std::vector<AlgoResult> rows;
        bool is_proposed = (a == "proposed");
        // Skip larger n values after a planning call exhausts the cumulative budget.
        std::map<double, bool> env_cut;

        for (size_t ci = 0; ci < param_sets.size(); ci++) {
            const auto &p = param_sets[ci];
            int n = param_scale[ci];
            std::string env = (p.p_idle == 0.004) ? "Low noise" : "High noise";

            if (env_cut[p.p_idle]) {
                std::cout << "  " << env << " n=" << std::setw(2) << n
                          << " ... skipped (cutoff)\n";
                continue;
            }

            std::cout << "  " << env << " n=" << std::setw(2) << n
                      << " (h=" << p.h << ", w=" << p.w << ") ... " << std::flush;

            const auto run = runBenchmark(p, *spec,
                                          budget_ms_per_plan_call * num_trials * num_iterations);
            env_cut[p.p_idle] = run.timed_out;
            std::vector<double> trial_rates, trial_times;
            for (const auto &trial : run.trials) {
                trial_rates.push_back(trial.filling_rate);
                trial_times.push_back(trial.runtime_ms);
            }
            const size_t trials_done = run.trials.size();
            const double sum = std::accumulate(trial_rates.begin(), trial_rates.end(), 0.0);
            const double rt = std::accumulate(trial_times.begin(), trial_times.end(), 0.0);
            const double per_op_ms = trials_done ? rt / trials_done / num_iterations : 0.0;
            const auto &all_per_op_times = run.per_op_planning_wall_ms;
            const double max_planning = all_per_op_times.empty() ? 0.0 :
                *std::max_element(all_per_op_times.begin(), all_per_op_times.end());
            const TrialSummary stats = trials_done ? run.trials.back() : TrialSummary{};

            AlgoResult r;
            r.timed_out = run.timed_out;
            r.num_operations_done = all_per_op_times.size();
            r.elapsed_ms = run.elapsed_ms;
            r.timeout_planning_wall_ms = run.timeout_planning_wall_ms;
            r.environment  = env;
            r.n            = n;
            r.h            = p.h;
            r.w            = p.w;
            r.m_r          = p.m_r;
            r.m_c          = p.m_c;
            r.p_idle     = p.p_idle;
            r.p_reload   = p.p_reload;
            r.filling_rate     = trials_done ? sum / trials_done : 0.0;
            r.filling_rate_std = sampleStd(trial_rates);
            r.runtime_ms       = trials_done ? rt / trials_done : 0.0;
            r.runtime_ms_std   = sampleStd(trial_times);
            r.max_planning_wall_ms = max_planning;
            r.per_op_planning_wall_ms_std = sampleStd(all_per_op_times);
            r.trial_filling_rates = trial_rates;
            r.trial_runtimes_ms   = trial_times;
            if (is_proposed && stats.hill_runs > 0)
                r.avg_convergence_iterations = static_cast<double>(stats.hill_iterations) / stats.hill_runs;
            if (stats.discarded_runs > 0)
                r.avg_discarded_atoms = static_cast<double>(stats.discarded_atoms) / stats.discarded_runs;
            rows.push_back(r);

            if (trials_done == 0) std::cout << "no completed trials";
            else std::cout << (r.filling_rate * 100) << "% (SD "
                      << (r.filling_rate_std * 100) << ", "
                      << r.runtime_ms << " planning ms/trial, "
                      << per_op_ms << " ms/op"
                      << ", max_plan=" << r.max_planning_wall_ms
                      << ", opSD=" << r.per_op_planning_wall_ms_std << " ms)";
            if (spec->get_mip_stats) {
                std::cout << "  [B&B:" << stats.mip_ok << " LP:" << stats.mip_fallback << "]";
            }
            if (r.avg_convergence_iterations >= 0) {
                std::cout << "  [HC convergence: " << r.avg_convergence_iterations
                          << " iters]";
            }
            if (env_cut[p.p_idle]) {
                std::cout << "  [TIMEOUT: cumulative plan budget="
                          << (budget_ms_per_plan_call * num_trials * num_iterations)
                          << " ms (" << budget_ms_per_plan_call << " ms/call x "
                          << (num_trials * num_iterations) << " calls), observed="
                          << r.timeout_planning_wall_ms
                          << " ms, completed trials=" << trials_done
                          << ", operations=" << r.num_operations_done << "]";
            }
            std::cout << "\n";
            // Save all results so far whenever an n completes or times out.
            writeJson(out, a, seed_base, num_trials, num_iterations, budget_ms_per_plan_call, rows);
        }

        if (rows.empty())
            writeJson(out, a, seed_base, num_trials, num_iterations, budget_ms_per_plan_call, rows);
    }

    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "Error: " << error.what() << "\n";
    return 1;
}
