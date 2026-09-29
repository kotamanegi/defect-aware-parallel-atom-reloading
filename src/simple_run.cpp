#include <iostream>
#include <vector>
#include <iomanip>
#include <cmath>
#include <fstream>
#include <string>
#include <filesystem>
#include "../include/types.h"
#include "../include/simulator.h"
#include "../include/algorithms.h"
#include "../include/experiment_metadata.h"

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

int main(int argc, char *argv[])
{
    // Set command-line defaults.
    int n = experiment_defaults::simple_n; // Default scale factor (the paper's simple-run experiment uses n=30).
    int algorithm = 1; // Default to Proposed; see the registry for all algorithm IDs.
    double p_idle = 0.004;   // Default for the low-noise case.
    double p_reload = 0.004; // Default matches p_idle.
    int num_iterations = experiment_defaults::iterations; // Total operations T=100, as in the paper.
    int num_trials = experiment_defaults::trials;    // Number of trials to average.
    int seed_base = experiment_defaults::seed_base;
    std::string output_file = "execution_result.json";

    // Parse command-line arguments.
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc)
        {
            n = std::atoi(argv[++i]);
        }
        else if (arg == "-a" && i + 1 < argc)
        {
            algorithm = std::atoi(argv[++i]);
        }
        else if (arg == "-p_idle" && i + 1 < argc)
        {
            p_idle = std::atof(argv[++i]);
        }
        else if (arg == "-p_reload" && i + 1 < argc)
        {
            p_reload = std::atof(argv[++i]);
        }
        else if (arg == "-i" && i + 1 < argc)
        {
            num_iterations = std::atoi(argv[++i]);
        }
        else if ((arg == "-t" || arg == "--trials") && i + 1 < argc)
        {
            num_trials = std::atoi(argv[++i]);
            if (num_trials < 1) num_trials = 1;
        }
        else if ((arg == "-s" || arg == "--seed") && i + 1 < argc)
        {
            seed_base = std::atoi(argv[++i]);
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc)
        {
            output_file = argv[++i];
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  -n <value>    Scale factor n; storage 12n x 30n, preparation 4n x 15n (default: 15)\n";
            std::cout << "  -a <value>    Algorithm (" << algorithmChoiceList() << ") (default: 1)\n";
            std::cout << "  -p_idle <value>   Environmental noise probability (default: 0.004)\n";
            std::cout << "  -p_reload <value> Transport failure probability during reloading (default: 0.004)\n";
            std::cout << "  -i <value>    Number of iterations (default: " << experiment_defaults::iterations << ")\n";
            std::cout << "  -t, --trials <value>  Number of trials (default: 100)\n";
            std::cout << "  -s, --seed <value>    Base random seed (default: 998244353)\n";
            std::cout << "  -o, --output <path> Output JSON file (default: execution_result.json)\n";
            std::cout << "  -h, --help Show help\n";
            return 0;
        }
    }

    if (num_iterations <= experiment_defaults::burn_in || num_trials <= 0) {
        std::cerr << "Error: The number of operations must exceed " << experiment_defaults::burn_in
                  << ", and the number of trials must be at least 1.\n";
        return 1;
    }

    // Set parameters using the geometry shared with benchmark mode (main.cpp).
    // The simulator runs one trial; the outer loop handles multiple trials.
    // Derive seeds deterministically from each trial_index (see noise.h).
    Parameters p = makeScaledParameters(n, p_idle, p_reload, num_iterations, 1);
    p.seed_base = static_cast<std::uint64_t>(seed_base);

    // Resolve the algorithm through the registry.
    const AlgorithmSpec *spec = findAlgorithmById(algorithm);
    if (spec == nullptr)
    {
        std::cerr << "Error: Invalid algorithm ID: " << algorithm << "\n";
        return 1;
    }
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
    for (int trial = 0; trial < num_trials; trial++) {
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
    for (size_t i = 0; i < T; i++) {
        double sum = 0.0;
        for (int trial = 0; trial < num_trials; trial++) {
            sum += trial_rates[trial][i];
        }
        mean_per_iter[i] = sum / num_trials;
    }
    // Compute the overall mean.
    double sum_avg = 0.0;
    for (int trial = 0; trial < num_trials; trial++) sum_avg += trial_avg[trial];
    double mean_avg_overall = sum_avg / num_trials;

    // Sample standard deviation (divide by n-1).
    if (num_trials > 1) {
        for (size_t i = 0; i < T; i++) {
            double sq = 0.0;
            for (int trial = 0; trial < num_trials; trial++) {
                double d = trial_rates[trial][i] - mean_per_iter[i];
                sq += d * d;
            }
            std_per_iter[i] = std::sqrt(sq / (num_trials - 1));
        }
    }

    std::cout << "Average filling rate (mean over trials): " << (mean_avg_overall * 100) << "%\n";

    // Write JSON output.
    // Create the output directory (e.g. results/) if needed.
    std::filesystem::path out_path(output_file);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream json_file(output_file);
    if (!json_file.is_open()) {
        std::cerr << "Failed to create JSON file: " << output_file << "\n";
        return 1;
    }
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
    for (size_t i = 0; i < T; i++) {
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
