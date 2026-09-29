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

#include "../../include/algorithms.h"
#include <scip/scip.h>
#include <scip/scipdefplugins.h>
#include <vector>
#include <set>
#include <cstdio>
#include <cstdlib>

static int s_bb_ok = 0;
static int s_lp_fallback = 0;

void resetMipStatsScip() { s_bb_ok = s_lp_fallback = 0; }
void getMipStatsScip(int &bb_ok, int &lp_fallback)
{
    bb_ok = s_bb_ok;
    lp_fallback = s_lp_fallback;
}

ReloadParams mipMethodScip(const Grid &A, int t, int m_r, int m_c, double p_op)
{
    int h = A.size();
    int w = A[0].size();

    std::vector<std::pair<int, int>> empty_pos;
    empty_pos.reserve(h * w / 4);
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            if (A[r][c] == 0)
                empty_pos.push_back({r, c});
    int ne = (int)empty_pos.size();

    // A no-op is optimal when there are no vacancies or capacity is zero.
    if (ne == 0 || m_r == 0 || m_c == 0)
        return {};

    SCIP *scip;
    SCIPcreate(&scip);
    SCIPincludeDefaultPlugins(scip);
    SCIPcreateProbBasic(scip, "reloading");
    SCIPsetObjsense(scip, SCIP_OBJSENSE_MAXIMIZE);
    SCIPsetIntParam(scip, "display/verblevel", 0);
    SCIPsetIntParam(scip, "presolving/maxrounds", 0);

    std::vector<SCIP_VAR *> x_vars(h), y_vars(w), z_vars(ne), mu_vars(m_c + 1), xi_vars(m_c + 1);
    char name[64];

    for (int r = 0; r < h; r++)
    {
        std::snprintf(name, sizeof(name), "x%d", r);
        SCIPcreateVarBasic(scip, &x_vars[r], name, 0.0, 1.0, 0.0,
                           SCIP_VARTYPE_BINARY);
        SCIPaddVar(scip, x_vars[r]);
    }
    for (int c = 0; c < w; c++)
    {
        std::snprintf(name, sizeof(name), "y%d", c);
        SCIPcreateVarBasic(scip, &y_vars[c], name, 0.0, 1.0, 0.0,
                           SCIP_VARTYPE_BINARY);
        SCIPaddVar(scip, y_vars[c]);
    }
    for (int k = 0; k < ne; k++)
    {
        std::snprintf(name, sizeof(name), "z%d", k);
        SCIPcreateVarBasic(scip, &z_vars[k], name, 0.0, 1.0, 1.0,
                           SCIP_VARTYPE_CONTINUOUS);
        SCIPaddVar(scip, z_vars[k]);
    }

    {
        SCIP_CONS *cons;
        std::vector<SCIP_Real> coefs_x(h, 1.0);
        std::vector<SCIP_Real> coefs_y(w, 1.0);

        SCIPcreateConsBasicLinear(scip, &cons, "sum_x", h, x_vars.data(),
                                  coefs_x.data(), -SCIPinfinity(scip), (SCIP_Real)m_r);
        SCIPaddCons(scip, cons);
        SCIPreleaseCons(scip, &cons);

        SCIPcreateConsBasicLinear(scip, &cons, "sum_y", w, y_vars.data(),
                                  coefs_y.data(), -SCIPinfinity(scip), (SCIP_Real)m_c);
        SCIPaddCons(scip, cons);
        SCIPreleaseCons(scip, &cons);
    }

    for (int b = 0; b <= m_c; ++b)
    {
        std::snprintf(name, sizeof(name), "mu%d", b);
        SCIPcreateVarBasic(scip, &mu_vars[b], name, 0.0, 1.0, 0.0,
                           SCIP_VARTYPE_BINARY);
        SCIPaddVar(scip, mu_vars[b]);
        std::snprintf(name, sizeof(name), "xi%d", b);
        SCIPcreateVarBasic(scip, &xi_vars[b], name, 0.0, SCIPinfinity(scip),
                           -p_op * b, SCIP_VARTYPE_CONTINUOUS);
        SCIPaddVar(scip, xi_vars[b]);
    }
    auto addEquality = [&](const char *label, std::vector<SCIP_VAR *> vars,
                           std::vector<SCIP_Real> coefs, double rhs) {
        SCIP_CONS *cons;
        SCIPcreateConsBasicLinear(scip, &cons, label, vars.size(), vars.data(),
                                  coefs.data(), rhs, rhs);
        SCIPaddCons(scip, cons);
        SCIPreleaseCons(scip, &cons);
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
        SCIP_CONS *cons;
        std::snprintf(name, sizeof(name), "xi_bound%d", b);
        SCIPcreateConsBasicLinear(scip, &cons, name, 2, bound_vars, bound_coefs,
                                  -SCIPinfinity(scip), 0.0);
        SCIPaddCons(scip, cons);
        SCIPreleaseCons(scip, &cons);
    }

    for (int k = 0; k < ne; k++)
    {
        int r = empty_pos[k].first;
        int c = empty_pos[k].second;

        {
            SCIP_VAR *vars[2] = {z_vars[k], x_vars[r]};
            SCIP_Real vals[2] = {1.0, -1.0};
            SCIP_CONS *cons;
            std::snprintf(name, sizeof(name), "zr%d_%d", r, k);
            SCIPcreateConsBasicLinear(scip, &cons, name, 2, vars, vals,
                                      -SCIPinfinity(scip), 0.0);
            SCIPaddCons(scip, cons);
            SCIPreleaseCons(scip, &cons);
        }
        {
            SCIP_VAR *vars[2] = {z_vars[k], y_vars[c]};
            SCIP_Real vals[2] = {1.0, -1.0};
            SCIP_CONS *cons;
            std::snprintf(name, sizeof(name), "zc%d_%d", c, k);
            SCIPcreateConsBasicLinear(scip, &cons, name, 2, vars, vals,
                                      -SCIPinfinity(scip), 0.0);
            SCIPaddCons(scip, cons);
            SCIPreleaseCons(scip, &cons);
        }
        {
            SCIP_VAR *vars[3] = {x_vars[r], y_vars[c], z_vars[k]};
            SCIP_Real vals[3] = {1.0, 1.0, -1.0};
            SCIP_CONS *cons;
            std::snprintf(name, sizeof(name), "zl%d_%d_%d", r, c, k);
            SCIPcreateConsBasicLinear(scip, &cons, name, 3, vars, vals,
                                      -SCIPinfinity(scip), 1.0);
            SCIPaddCons(scip, cons);
            SCIPreleaseCons(scip, &cons);
        }
    }

    // Solve exactly with no solver time limit.
    SCIPsolve(scip);

    if (SCIPgetStatus(scip) != SCIP_STATUS_OPTIMAL)
    {
        std::fprintf(stderr, "SCIP: B&B failed (status=%d)\n",
                     (int)SCIPgetStatus(scip));
        std::exit(1);
    }

    SCIP_SOL *sol = SCIPgetBestSol(scip);
    ReloadParams params;
    for (int r = 0; r < h; r++)
        if (SCIPgetSolVal(scip, sol, x_vars[r]) > 0.5)
            params.R.insert(r);
    for (int c = 0; c < w; c++)
        if (SCIPgetSolVal(scip, sol, y_vars[c]) > 0.5)
            params.C.insert(c);
    ++s_bb_ok;

    for (int r = 0; r < h; r++)
        SCIPreleaseVar(scip, &x_vars[r]);
    for (int c = 0; c < w; c++)
        SCIPreleaseVar(scip, &y_vars[c]);
    for (int k = 0; k < ne; k++)
        SCIPreleaseVar(scip, &z_vars[k]);

    for (int b = 0; b <= m_c; ++b) {
        SCIPreleaseVar(scip, &mu_vars[b]);
        SCIPreleaseVar(scip, &xi_vars[b]);
    }

    SCIPfree(&scip);

    (void)t;
    return params;
}
