#include "algorithms.h"
#include <ostream>

namespace {
void writeRowByRowJsonMetadata(std::ostream &out)
{
    out << "  \"experiment_role\": \"additional_row_by_row_comparison\",\n";
    out << "  \"row_selection_policy\": \"cyclic row (t-1) mod h; full rows consume a step\",\n";
    out << "  \"column_selection_policy\": \"defects only, ascending column index, at most m_c\",\n";
    out << "  \"time_model\": \"one row attempt per unit-time operation; same per-step noise as main experiments\",\n";
    out << "  \"interpretation\": \"finite-horizon mean; burn-in does not establish stationarity for this comparator\",\n";
}
} // namespace

// Algorithm registry.
//
// IDs are exposed in result JSON ("algorithm") and the simple-run -a option.
// Keep existing IDs unchanged; 2 is reserved for the removed Proposed 2 (alternating greedy).
// mip_scip retains ID 3 from the former "mip" method, whose default solver was SCIP.
static const std::vector<AlgorithmSpec> kAlgorithms = {
    {0, "baseline",      "Baseline",      baselineMethod,      nullptr,           nullptr,               false, 6,  nullptr},
    {1, "proposed",      "Proposed",      proposedMethod,      nullptr,           nullptr,               true,  6,  nullptr},
    {3, "mip_scip",      "MIP (SCIP)",    mipMethodScip,       resetMipStatsScip, getMipSolvedCountScip, false, 6,  nullptr},
    {4, "greedy",        "Greedy",        greedyMethod,        nullptr,           nullptr,               false, 6,  nullptr},
    {5, "simple_greedy", "Simple Greedy", simpleGreedyMethod,  nullptr,           nullptr,               false, 6,  nullptr},
    {6, "mip_glpk",      "MIP (GLPK)",    mipMethodGlpk,       resetMipStatsGlpk, getMipSolvedCountGlpk, false, 6,  nullptr},
    {7, "row_by_row",    "Row-by-row",    rowByRowMethod,      nullptr,           nullptr,               false, 12, writeRowByRowJsonMetadata},
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
