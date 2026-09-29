#include "../include/types.h"

// Planning RNG, separate from physical noise.
std::mt19937 rng;

Parameters makeScaledParameters(int n, double p_idle, double p_reload,
                                int num_iterations, int num_trials)
{
    Parameters p;
    p.h = 12 * n;
    p.w = 30 * n;
    p.m_r = 4 * n;
    p.m_c = 15 * n;
    p.p_idle = p_idle;
    p.p_reload = p_reload;
    p.num_iterations = num_iterations;
    p.num_trials = num_trials;
    return p;
}
