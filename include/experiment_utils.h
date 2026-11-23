#ifndef EXPERIMENT_UTILS_H
#define EXPERIMENT_UTILS_H

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace experiment_utils {

inline std::string requireArgValue(int &index, int argc, char *argv[], const std::string &flag)
{
    if (index + 1 >= argc)
        throw std::invalid_argument(flag + " requires a value");
    return argv[++index];
}

inline int parseIntArg(const std::string &flag, const std::string &value)
{
    size_t parsed = 0;
    int result = 0;
    try {
        result = std::stoi(value, &parsed);
    } catch (const std::exception &) {
        throw std::invalid_argument(flag + " must be an integer: " + value);
    }
    if (parsed != value.size())
        throw std::invalid_argument(flag + " must be an integer: " + value);
    return result;
}

inline std::uint64_t parseUint64Arg(const std::string &flag, const std::string &value)
{
    if (value.empty() || value[0] == '-')
        throw std::invalid_argument(flag + " must be a non-negative integer: " + value);
    size_t parsed = 0;
    unsigned long long result = 0;
    try {
        result = std::stoull(value, &parsed);
    } catch (const std::exception &) {
        throw std::invalid_argument(flag + " must be a non-negative integer: " + value);
    }
    if (parsed != value.size())
        throw std::invalid_argument(flag + " must be a non-negative integer: " + value);
    return static_cast<std::uint64_t>(result);
}

inline double parseDoubleArg(const std::string &flag, const std::string &value)
{
    size_t parsed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &parsed);
    } catch (const std::exception &) {
        throw std::invalid_argument(flag + " must be a finite number: " + value);
    }
    if (parsed != value.size() || !std::isfinite(result))
        throw std::invalid_argument(flag + " must be a finite number: " + value);
    return result;
}

inline double parseProbabilityArg(const std::string &flag, const std::string &value)
{
    const double p = parseDoubleArg(flag, value);
    if (p < 0.0 || p > 1.0)
        throw std::invalid_argument(flag + " must be in [0, 1]");
    return p;
}

inline double parseBudgetMsArg(const std::string &flag, const std::string &value)
{
    const double ms = parseDoubleArg(flag, value);
    if (ms <= 0.0 || ms >= static_cast<double>(std::numeric_limits<long long>::max()) / 1000.0)
        throw std::invalid_argument(flag + " must be a finite positive duration in milliseconds");
    return ms;
}

// Sample standard deviation (divides by n - 1; returns 0 when n < 2).
inline double sampleStd(const std::vector<double> &values)
{
    if (values.size() < 2) return 0.0;
    double mean = 0.0;
    for (double x : values) mean += x;
    mean /= static_cast<double>(values.size());
    double sq = 0.0;
    for (double x : values) {
        const double d = x - mean;
        sq += d * d;
    }
    return std::sqrt(sq / static_cast<double>(values.size() - 1));
}

inline void ensureParentDirectory(const std::string &filename)
{
    const std::filesystem::path out_path(filename);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }
}

inline void writeJsonDoubleArray(std::ostream &out, const std::vector<double> &values)
{
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i];
    }
    out << "]";
}

} // namespace experiment_utils

#endif // EXPERIMENT_UTILS_H
