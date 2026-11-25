#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "types.h"
#include "simulator.h"
#include "algorithms.h"
#include "experiment_metadata.h"
#include "experiment_utils.h"

// Join registered methods as "0:Baseline, 1:Proposed, ..." for help output.
static std::string algorithmChoiceList()
{
    std::string s;
    for (const auto &spec : algorithms()) {
        if (!s.empty()) s += ", ";
        s += std::to_string(spec.id) + ":" + spec.display_name;
    }
    return s;
}

static void printUsage(const char *prog)
{
    std::cout << "Usage: " << prog << " [options]\n";
    std::cout << "Options:\n";
    std::cout << "  -n <value>    Scale factor n; storage 12n x 30n, preparation 4n x 15n (default: "
              << experiment_defaults::simple_n << ")\n";
    std::cout << "  -a <value>    Algorithm (" << algorithmChoiceList() << ") (default: 1)\n";
    std::cout << "  -p_idle <value>   Environmental noise probability (default: "
              << experiment_defaults::low_noise << ")\n";
    std::cout << "  -p_reload <value> Transport failure probability during reloading (default: "
              << experiment_defaults::low_noise << ")\n";
    std::cout << "  -i <value>    Number of iterations (default: " << experiment_defaults::iterations << ")\n";
    std::cout << "  -t, --trials <value>  Number of trials (default: " << experiment_defaults::trials << ")\n";
    std::cout << "  -s, --seed <value>    Base random seed (default: " << experiment_defaults::seed_base << ")\n";
    std::cout << "  -o, --output <path> Output JSON file (default: execution_result.json)\n";
    std::cout << "  -h, --help Show help\n";
}

int main(int argc, char *argv[]) try
{
    // Set command-line defaults.
    int n = experiment_defaults::simple_n; // Default scale factor (n=15).
    int algorithm = 1;                     // Default to Proposed; see the registry for all algorithm IDs.
    double p_idle = experiment_defaults::low_noise;
    double p_reload = experiment_defaults::low_noise;
    int num_iterations = experiment_defaults::iterations; // Total operations T=100, as in the paper.
    int num_trials = experiment_defaults::trials;         // Number of trials to average.
    std::uint64_t seed_base = experiment_defaults::seed_base;
    std::string output_file = "execution_result.json";

    // Parse command-line arguments.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-n") {
            n = experiment_utils::parseIntArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-a") {
            algorithm = experiment_utils::parseIntArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-p_idle") {
            p_idle = experiment_utils::parseProbabilityArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-p_reload") {
            p_reload = experiment_utils::parseProbabilityArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-i") {
            num_iterations = experiment_utils::parseIntArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-t" || arg == "--trials") {
            num_trials = experiment_utils::parseIntArg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-s" || arg == "--seed") {
            seed_base = experiment_utils::parseUint64Arg(arg, experiment_utils::requireArgValue(i, argc, argv, arg));
        } else if (arg == "-o" || arg == "--output") {
            output_file = experiment_utils::requireArgValue(i, argc, argv, arg);
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            throw std::invalid_argument("Unknown option: " + arg);
        }
    }

    if (n < experiment_defaults::n_min) {
        throw std::invalid_argument("-n must be at least " +
                                    std::to_string(experiment_defaults::n_min));
    }
    if (num_iterations <= experiment_defaults::burn_in || num_trials <= 0) {
        throw std::invalid_argument(
            "The number of operations must exceed " + std::to_string(experiment_defaults::burn_in) +
            ", and the number of trials must be at least 1.");
    }

    // Set parameters using the geometry shared with benchmark mode (main.cpp).
    // The simulator runs one trial; the outer loop handles multiple trials.
    // Derive seeds deterministically from each trial_index (see noise.h).
    Parameters p = makeScaledParameters(n, p_idle, p_reload, num_iterations, 1);
    p.seed_base = seed_base;

    // Resolve the algorithm through the registry.
    const AlgorithmSpec *spec = findAlgorithmById(algorithm);
    if (spec == nullptr)
        throw std::invalid_argument("Invalid algorithm ID: " + std::to_string(algorithm));
    const std::string algorithm_name = spec->display_name;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "Atom Reloading Simulation - Simple-Run Mode (trials=" << num_trials << ")\n";
    std::cout << "============================================\n";
    std::cout << "Grid size (h): " << p.h << "\n";
    std::cout << "Grid size (w): " << p.w << "\n";
    std::cout << "Preparation region size (m_r×m_c): " << p.m_r << "×" << p.m_c << "\n";
    std::cout << "Environmental noise (p_idle): " << p.p_idle << "\n";
    std::cout << "Reloading noise (p_reload): " << p.p_reload << "\n";
    std::cout << "Number of iterations: " << p.num_iterations << "\n";
    std::cout << "Algorithm: " << algorithm_name << "\n";
    std::cout << "Seed base: " << seed_base << "\n";
    std::cout << "Random number policy: fixed seed per trial index (independent of execution order, method, and n_max)\n";
    std::cout << "--------------------------------------------\n";

    // Keep only the per-iteration filling rates for each trial.
    // Do not store grid states because they are never read.
    std::vector<std::vector<double>> trial_rates;
    trial_rates.reserve(num_trials);
    std::vector<double> trial_avg(num_trials, 0.0);
    for (int trial = 0; trial < num_trials; ++trial) {
        Parameters trial_p = p;
        trial_p.trial_index = static_cast<std::uint64_t>(trial);
        std::vector<double> per_iter;
        SimulationTrace trace;
        trace.per_iter_filling_rate = &per_iter;
        // As in benchmark mode, exclude the first 20 operations from the mean.
        trial_avg[trial] = runSimulation(trial_p, spec->id, trace);

        trial_rates.push_back(std::move(per_iter));
        if ((trial + 1) % 10 == 0 || trial == num_trials - 1) {
            std::cout << "  trial " << (trial + 1) << "/" << num_trials
                      << " done (avg_fill=" << trial_avg[trial] * 100.0 << "%)\n";
        }
    }

    // Compute the mean and sample standard deviation across trials for each iteration.
    const size_t T = trial_rates[0].size();
    std::vector<double> mean_per_iter(T, 0.0);
    std::vector<double> std_per_iter(T, 0.0);
    std::vector<double> column(num_trials, 0.0);
    for (size_t i = 0; i < T; ++i) {
        double sum = 0.0;
        for (int trial = 0; trial < num_trials; ++trial) {
            column[trial] = trial_rates[trial][i];
            sum += column[trial];
        }
        mean_per_iter[i] = sum / num_trials;
        std_per_iter[i] = experiment_utils::sampleStd(column);
    }
    const double mean_avg_overall =
        std::accumulate(trial_avg.begin(), trial_avg.end(), 0.0) / num_trials;

    std::cout << "Average filling rate (mean over trials): " << (mean_avg_overall * 100.0) << "%\n";

    // Write JSON output.
    experiment_utils::ensureParentDirectory(output_file);

    std::ofstream json_file(output_file);
    if (!json_file.is_open())
        throw std::runtime_error("Failed to create JSON file: " + output_file);

    json_file << std::fixed << std::setprecision(6);
    json_file << "{\n";
    json_file << "  \"parameters\": {\n";
    json_file << "    \"n\": " << n << ",\n";
    json_file << "    \"h\": " << p.h << ",\n";
    json_file << "    \"w\": " << p.w << ",\n";
    json_file << "    \"m_r\": " << p.m_r << ",\n";
    json_file << "    \"m_c\": " << p.m_c << ",\n";
    json_file << "    \"p_idle\": " << p.p_idle << ",\n";
    json_file << "    \"p_reload\": " << p.p_reload << ",\n";
    json_file << "    \"num_iterations\": " << p.num_iterations << ",\n";
    json_file << "    \"num_trials\": " << num_trials << ",\n";
    json_file << "    \"seed_base\": " << seed_base << "\n";
    json_file << "  },\n";
    json_file << "  \"algorithm\": {\n";
    json_file << "    \"id\": " << algorithm << ",\n";
    json_file << "    \"name\": \"" << spec->name << "\"\n";
    json_file << "  },\n";
    json_file << "  \"average_filling_rate\": " << mean_avg_overall << ",\n";
    writeExperimentMetadata(json_file, num_trials);
    json_file << "  \"iterations\": [\n";
    for (size_t i = 0; i < T; ++i) {
        json_file << "    {\n";
        json_file << "      \"iteration\": " << (i + 1) << ",\n";
        json_file << "      \"filling_rate\": " << mean_per_iter[i] << ",\n";
        json_file << "      \"filling_rate_std\": " << std_per_iter[i] << "\n";
        json_file << "    }";
        if (i + 1 < T) json_file << ",";
        json_file << "\n";
    }
    json_file << "  ]\n";
    json_file << "}\n";
    json_file.close();
    std::cout << "\nResults saved to " << output_file << ".\n";

    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "Error: " << error.what() << "\n";
    return 1;
}
