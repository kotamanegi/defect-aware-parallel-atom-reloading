#ifndef TIMING_H
#define TIMING_H
#include <chrono>
#include <utility>

using WallClock = std::chrono::steady_clock;

// Measure only the planning call, excluding result aggregation and noise generation.
template<class Function>
auto measureWallTime(Function &&function)
{
    const auto start = WallClock::now();
    auto value = std::forward<Function>(function)();
    const auto stop = WallClock::now();
    return std::make_pair(std::move(value),
        std::chrono::duration<double, std::milli>(stop - start).count());
}
#endif
