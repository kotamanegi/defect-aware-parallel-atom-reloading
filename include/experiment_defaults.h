#ifndef EXPERIMENT_DEFAULTS_H
#define EXPERIMENT_DEFAULTS_H
#include <cstdint>

namespace experiment_defaults {
inline constexpr int n_min = 1;
inline constexpr int n_max = 15;
inline constexpr int simple_n = n_max;
inline constexpr int iterations = 100;
inline constexpr int trials = 100;
inline constexpr int burn_in = 20;
inline constexpr std::uint64_t seed_base = 998244353;
inline constexpr double low_noise = 0.004;
inline constexpr double high_noise = 0.02;
}
#endif
