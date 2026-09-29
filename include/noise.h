#ifndef NOISE_H
#define NOISE_H
#include "types.h"
#include <cstdint>

// v1 fixes seed_seq input order, IEEE754 probability representation, and uniform53 conversion.
inline constexpr const char *noise_policy = "site_noise_v1_mt19937_seed_seq_uniform53";
enum class RandomStream : std::uint32_t { Idle = 1, Reload = 2, Planning = 3 };
std::mt19937 makeTrialEngine(const Parameters &p, RandomStream stream);
using NoiseMask = std::vector<std::vector<unsigned char>>;
struct StepNoise {
    NoiseMask idle_loss;
    NoiseMask reload_failure;
};

// Generate both noise types at every site each step, independently of state and operations.
// Store only one step; references from nextStep() remain valid until the next call.
class TrialNoise {
public:
    explicit TrialNoise(const Parameters &p);
    const StepNoise &nextStep();
private:
    std::mt19937 idle_rng_, reload_rng_;
    double p_idle_, p_reload_;
    StepNoise step_;
};
#endif
