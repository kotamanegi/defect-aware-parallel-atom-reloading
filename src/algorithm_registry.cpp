#include "../include/algorithms.h"

// Algorithm registry.
//
// IDs are exposed in result JSON ("algorithm") and the simple-run -a option.
// Keep existing IDs unchanged; 2 is reserved for the removed Proposed 2 (alternating greedy).
// mip_scip retains ID 3 from the former "mip" method, whose default solver was SCIP.
static const std::vector<AlgorithmSpec> kAlgorithms = {
    {0, "baseline",      "Baseline",      baselineMethod,      nullptr,            nullptr},
    {1, "proposed",      "Proposed",      proposedMethod,      nullptr,            nullptr},
    {3, "mip_scip",      "MIP (SCIP)",    mipMethodScip,       resetMipStatsScip,  getMipStatsScip},
    {4, "greedy",        "Greedy",        greedyMethod,        nullptr,            nullptr},
    {5, "simple_greedy", "Simple Greedy", simpleGreedyMethod,  nullptr,            nullptr},
    {6, "mip_glpk",      "MIP (GLPK)",    mipMethodGlpk,       resetMipStatsGlpk,  getMipStatsGlpk},
    {7, "row_by_row",    "Row-by-row",    rowByRowMethod,      nullptr,            nullptr},
};

const std::vector<AlgorithmSpec> &algorithms()
{
    return kAlgorithms;
}

const AlgorithmSpec *findAlgorithmByName(const std::string &name)
{
    for (const auto &spec : kAlgorithms)
        if (name == spec.name)
            return &spec;
    return nullptr;
}

const AlgorithmSpec *findAlgorithmById(int id)
{
    for (const auto &spec : kAlgorithms)
        if (id == spec.id)
            return &spec;
    return nullptr;
}
