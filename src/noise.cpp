#include "../include/noise.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
std::uint64_t probabilityBits(double p)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t) &&
                  std::numeric_limits<double>::is_iec559 &&
                  std::numeric_limits<double>::digits == 53, "IEEE754 binary64 required");
    if (!std::isfinite(p) || p < 0 || p > 1)
        throw std::invalid_argument("noise probability must be in [0,1]");
    if (p == 0) p = 0.0; // Treat -0 and +0 as the same condition.
    std::uint64_t bits;
    std::memcpy(&bits, &p, sizeof(bits));
    return bits;
}

double uniform53(std::mt19937 &engine)
{
    // Avoid implementation differences in std::uniform_real_distribution and RNG consumption.
    const std::uint64_t hi = engine() >> 5;
    const std::uint64_t lo = engine() >> 6;
    return static_cast<double>((hi << 26) | lo) * 0x1.0p-53;
}
}

std::mt19937 makeTrialEngine(const Parameters &p, RandomStream stream)
{
    if (p.h <= 0 || p.w <= 0 || p.m_r < 0 || p.m_r > p.h ||
        p.m_c < 0 || p.m_c > p.w)
        throw std::invalid_argument("invalid noise grid/capacity");
    std::vector<std::uint32_t> words{1}; // seed policy version
    auto append64 = [&](std::uint64_t v) {
        words.push_back(static_cast<std::uint32_t>(v));
        words.push_back(static_cast<std::uint32_t>(v >> 32));
    };
    append64(p.seed_base);
    for (int value : {p.h,p.w,p.m_r,p.m_c}) words.push_back(static_cast<std::uint32_t>(value));
    append64(probabilityBits(p.p_idle));
    append64(probabilityBits(p.p_reload));
    append64(p.trial_index);
    words.push_back(static_cast<std::uint32_t>(stream));
    std::seed_seq seed(words.begin(), words.end());
    return std::mt19937(seed);
}

TrialNoise::TrialNoise(const Parameters &p)
    : idle_rng_(makeTrialEngine(p, RandomStream::Idle)),
      reload_rng_(makeTrialEngine(p, RandomStream::Reload)),
      p_idle_(p.p_idle), p_reload_(p.p_reload),
      step_{NoiseMask(p.h, std::vector<unsigned char>(p.w)),
            NoiseMask(p.h, std::vector<unsigned char>(p.w))}
{}

const StepNoise &TrialNoise::nextStep()
{
    for (size_t r = 0; r < step_.idle_loss.size(); ++r)
        for (size_t c = 0; c < step_.idle_loss[r].size(); ++c) {
            step_.idle_loss[r][c] = uniform53(idle_rng_) < p_idle_;
            step_.reload_failure[r][c] = uniform53(reload_rng_) < p_reload_;
        }
    return step_;
}
