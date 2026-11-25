// Variable-size MIP from the paper: x_r and y_c are binary row/column selection variables.
// Define continuous 0 <= z_k <= 1 only at vacancies, with z_k <= x_r, y_c.
// Positive objective coefficients give z_k = x_r * y_c at the optimum.
// One-hot binary variables mu_b for column counts b=0..m_c, and continuous xi_b >= 0:
//   sum(x) <= m_r, sum(y) <= m_c, sum(mu) = 1,
//   sum(y) = sum(b*mu_b), sum(xi) = sum(x), xi_b <= m_r*mu_b.
// Maximize sum(z) - p_op*sum(b*xi_b); the latter penalizes operation failures over the selected area.
// z_k is continuous and defined only at vacancies. Impose all McCormick bounds
//   z <= x, z <= y, z >= x + y - 1
// from the paper. Nonnegative coefficients (vacancy=1) enforce z = x*y at integer optima.
// Find an optimal solution without a solver time limit or heuristic fallback.

#include "algorithms.h"
#include "planning_utils.h"
#include <scip/scip.h>
#include <scip/scipdefplugins.h>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int s_bb_ok = 0;

void checkScip(SCIP_RETCODE rc, const char *action)
{
    if (rc != SCIP_OKAY) {
        throw std::runtime_error(std::string("SCIP: ") + action +
                                 " failed (retcode=" + std::to_string(static_cast<int>(rc)) + ")");
    }
}

class ScipModel {
public:
    ScipModel()
    {
        checkScip(SCIPcreate(&scip_), "SCIPcreate");
        checkScip(SCIPincludeDefaultPlugins(scip_), "SCIPincludeDefaultPlugins");
        checkScip(SCIPcreateProbBasic(scip_, "reloading"), "SCIPcreateProbBasic");
        checkScip(SCIPsetObjsense(scip_, SCIP_OBJSENSE_MAXIMIZE), "SCIPsetObjsense");
        checkScip(SCIPsetIntParam(scip_, "display/verblevel", 0), "set display/verblevel");
        checkScip(SCIPsetIntParam(scip_, "presolving/maxrounds", 0), "set presolving/maxrounds");
    }

    ~ScipModel()
    {
        if (scip_) {
            for (SCIP_VAR *&var : owned_vars_) {
                if (var) SCIPreleaseVar(scip_, &var);
            }
            SCIPfree(&scip_);
        }
    }

    ScipModel(const ScipModel &) = delete;
    ScipModel &operator=(const ScipModel &) = delete;

    SCIP *get() const { return scip_; }

    SCIP_VAR *addVar(const char *name, SCIP_Real lb, SCIP_Real ub,
                     SCIP_Real obj, SCIP_VARTYPE type)
    {
        SCIP_VAR *var = nullptr;
        checkScip(SCIPcreateVarBasic(scip_, &var, name, lb, ub, obj, type), "SCIPcreateVarBasic");
        owned_vars_.push_back(var);
        checkScip(SCIPaddVar(scip_, var), "SCIPaddVar");
        return var;
    }

    void addLinearCons(const char *name, int nvars, SCIP_VAR **vars,
                       SCIP_Real *vals, SCIP_Real lhs, SCIP_Real rhs)
    {
        SCIP_CONS *cons = nullptr;
        checkScip(SCIPcreateConsBasicLinear(scip_, &cons, name, nvars, vars, vals, lhs, rhs),
                  "SCIPcreateConsBasicLinear");
        const SCIP_RETCODE add_rc = SCIPaddCons(scip_, cons);
        const SCIP_RETCODE rel_rc = SCIPreleaseCons(scip_, &cons);
        checkScip(add_rc, "SCIPaddCons");
        checkScip(rel_rc, "SCIPreleaseCons");
    }

private:
    SCIP *scip_ = nullptr;
    std::vector<SCIP_VAR *> owned_vars_;
};
} // namespace

void resetMipStatsScip() { s_bb_ok = 0; }
int getMipSolvedCountScip() { return s_bb_ok; }

ReloadParams mipMethodScip(const Grid &A, int /*t*/, int m_r, int m_c, double p_op)
{
    const auto [h, w] = planning_detail::dimensions(A, m_r, m_c, p_op);
    if (h == 0 || w == 0 || m_r == 0 || m_c == 0)
        return {};

    const auto empty_pos = planning_detail::collectVacancies(A);
    const int ne = static_cast<int>(empty_pos.size());
    // A no-op is optimal when there are no vacancies.
    if (ne == 0)
        return {};

    ScipModel model;
    SCIP *scip = model.get();
    const SCIP_Real inf = SCIPinfinity(scip);

    std::vector<SCIP_VAR *> x_vars(h), y_vars(w), z_vars(ne), mu_vars(m_c + 1), xi_vars(m_c + 1);
    char name[64];

    for (int r = 0; r < h; ++r) {
        std::snprintf(name, sizeof(name), "x%d", r);
        x_vars[r] = model.addVar(name, 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY);
    }
    for (int c = 0; c < w; ++c) {
        std::snprintf(name, sizeof(name), "y%d", c);
        y_vars[c] = model.addVar(name, 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY);
    }
    for (int k = 0; k < ne; ++k) {
        std::snprintf(name, sizeof(name), "z%d", k);
        z_vars[k] = model.addVar(name, 0.0, 1.0, 1.0, SCIP_VARTYPE_CONTINUOUS);
    }

    {
        std::vector<SCIP_Real> coefs_x(h, 1.0);
        std::vector<SCIP_Real> coefs_y(w, 1.0);
        model.addLinearCons("sum_x", h, x_vars.data(), coefs_x.data(), -inf, static_cast<SCIP_Real>(m_r));
        model.addLinearCons("sum_y", w, y_vars.data(), coefs_y.data(), -inf, static_cast<SCIP_Real>(m_c));
    }

    for (int b = 0; b <= m_c; ++b) {
        std::snprintf(name, sizeof(name), "mu%d", b);
        mu_vars[b] = model.addVar(name, 0.0, 1.0, 0.0, SCIP_VARTYPE_BINARY);
        std::snprintf(name, sizeof(name), "xi%d", b);
        xi_vars[b] = model.addVar(name, 0.0, inf, -p_op * b, SCIP_VARTYPE_CONTINUOUS);
    }

    auto addEquality = [&](const char *label, std::vector<SCIP_VAR *> vars,
                           std::vector<SCIP_Real> coefs, double rhs) {
        model.addLinearCons(label, static_cast<int>(vars.size()), vars.data(), coefs.data(), rhs, rhs);
    };
    addEquality("one_hot", mu_vars, std::vector<SCIP_Real>(m_c + 1, 1.0), 1.0);

    std::vector<SCIP_VAR *> vars = y_vars;
    std::vector<SCIP_Real> coefs(w, 1.0);
    for (int b = 1; b <= m_c; ++b) {
        vars.push_back(mu_vars[b]);
        coefs.push_back(-b);
    }
    addEquality("column_count", vars, coefs, 0.0);

    vars = x_vars;
    coefs.assign(h, -1.0);
    for (int b = 0; b <= m_c; ++b) {
        vars.push_back(xi_vars[b]);
        coefs.push_back(1.0);
    }
    addEquality("row_count", vars, coefs, 0.0);

    for (int b = 0; b <= m_c; ++b) {
        SCIP_VAR *bound_vars[] = {xi_vars[b], mu_vars[b]};
        SCIP_Real bound_coefs[] = {1.0, -static_cast<double>(m_r)};
        std::snprintf(name, sizeof(name), "xi_bound%d", b);
        model.addLinearCons(name, 2, bound_vars, bound_coefs, -inf, 0.0);
    }

    for (int k = 0; k < ne; ++k) {
        const int r = empty_pos[k].first;
        const int c = empty_pos[k].second;

        SCIP_VAR *zr_vars[2] = {z_vars[k], x_vars[r]};
        SCIP_Real zr_vals[2] = {1.0, -1.0};
        std::snprintf(name, sizeof(name), "zr%d_%d", r, k);
        model.addLinearCons(name, 2, zr_vars, zr_vals, -inf, 0.0);

        SCIP_VAR *zc_vars[2] = {z_vars[k], y_vars[c]};
        SCIP_Real zc_vals[2] = {1.0, -1.0};
        std::snprintf(name, sizeof(name), "zc%d_%d", c, k);
        model.addLinearCons(name, 2, zc_vars, zc_vals, -inf, 0.0);

        SCIP_VAR *zl_vars[3] = {x_vars[r], y_vars[c], z_vars[k]};
        SCIP_Real zl_vals[3] = {1.0, 1.0, -1.0};
        std::snprintf(name, sizeof(name), "zl%d_%d_%d", r, c, k);
        model.addLinearCons(name, 3, zl_vars, zl_vals, -inf, 1.0);
    }

    // Solve exactly with no solver time limit.
    checkScip(SCIPsolve(scip), "SCIPsolve");

    if (SCIPgetStatus(scip) != SCIP_STATUS_OPTIMAL) {
        throw std::runtime_error("SCIP: B&B failed (status=" +
                                 std::to_string(static_cast<int>(SCIPgetStatus(scip))) + ")");
    }

    SCIP_SOL *sol = SCIPgetBestSol(scip);
    ReloadParams params;
    for (int r = 0; r < h; ++r)
        if (SCIPgetSolVal(scip, sol, x_vars[r]) > 0.5)
            params.R.insert(r);
    for (int c = 0; c < w; ++c)
        if (SCIPgetSolVal(scip, sol, y_vars[c]) > 0.5)
            params.C.insert(c);
    ++s_bb_ok;

    return params;
}
