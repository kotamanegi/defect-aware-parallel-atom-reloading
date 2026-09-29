// Variable-size MIP from the paper: x_r and y_c are binary row/column selection variables.
// Define continuous 0 <= z_k <= 1 only at vacancies, with z_k <= x_r, y_c.
// Positive objective coefficients give z_k = x_r * y_c at the optimum.
// Also impose the paper's McCormick lower bound z_k >= x_r + y_c - 1.
// One-hot binary variables mu_b for column counts b=0..m_c, and continuous xi_b >= 0:
//   sum(x) <= m_r, sum(y) <= m_c, sum(mu) = 1,
//   sum(y) = sum(b*mu_b), sum(xi) = sum(x), xi_b <= m_r*mu_b.
// Maximize sum(z) - p_op*sum(b*xi_b); the latter penalizes operation failures over the selected area.
// Find an optimal solution without a solver time limit or heuristic fallback.

#include "../../include/algorithms.h"
#include <glpk.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>

static int s_bb_ok = 0;
static int s_lp_fallback = 0;

void resetMipStatsGlpk() { s_bb_ok = s_lp_fallback = 0; }
void getMipStatsGlpk(int &bb_ok, int &lp_fallback)
{
    bb_ok = s_bb_ok;
    lp_fallback = s_lp_fallback;
}

// MIP-based method using GLPK.
ReloadParams mipMethodGlpk(const Grid &A, int t, int m_r, int m_c, double p_op)
{
    int h = A.size();
    int w = A[0].size();

    // Enumerate vacant sites.
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

    // Build the GLPK model.
    // Variable indices (one-based):
    //   x_r: 1 .. h
    //   y_c: h+1 .. h+w
    //   z_k: h+w+1 .. h+w+ne
    glp_prob *mip = glp_create_prob();
    glp_set_obj_dir(mip, GLP_MAX);

    const int mu_start = h + w + ne + 1;
    const int xi_start = mu_start + m_c + 1;
    int total_vars = h + w + ne + 2 * (m_c + 1);
    glp_add_cols(mip, total_vars);

    // x_r: binary
    for (int r = 0; r < h; r++)
        glp_set_col_kind(mip, r + 1, GLP_BV);

    // y_c: binary
    for (int c = 0; c < w; c++)
        glp_set_col_kind(mip, h + c + 1, GLP_BV);

    // z_k: continuous, with objective coefficient 1.
    for (int k = 0; k < ne; k++)
    {
        glp_set_col_bnds(mip, h + w + k + 1, GLP_DB, 0.0, 1.0);
        glp_set_obj_coef(mip, h + w + k + 1, 1.0);
    }

    for (int b = 0; b <= m_c; ++b) {
        glp_set_col_kind(mip, mu_start + b, GLP_BV);
        glp_set_col_bnds(mip, xi_start + b, GLP_LO, 0.0, 0.0);
        glp_set_obj_coef(mip, xi_start + b, -p_op * b);
    }

    // Rows: 2 capacity, 2*ne vacancy upper bounds, ne lower bounds, 3 count equalities, m_c+1 xi upper bounds.
    const int one_hot = 3 + 3 * ne;
    const int column_count = one_hot + 1;
    const int row_count = one_hot + 2;
    const int xi_bound = one_hot + 3;
    glp_add_rows(mip, 2 + 3 * ne + 3 + m_c + 1);
    glp_set_row_bnds(mip, 1, GLP_UP, 0.0, m_r);
    glp_set_row_bnds(mip, 2, GLP_UP, 0.0, m_c);
    glp_set_row_bnds(mip, one_hot, GLP_FX, 1.0, 1.0);
    glp_set_row_bnds(mip, column_count, GLP_FX, 0.0, 0.0);
    glp_set_row_bnds(mip, row_count, GLP_FX, 0.0, 0.0);

    // GLPK sparse matrices use one-based indices.
    std::vector<int> ia(1), ja(1);
    std::vector<double> ar(1);
    auto coefficient = [&](int row, int col, double value) {
        if (value == 0.0) return;
        ia.push_back(row);
        ja.push_back(col);
        ar.push_back(value);
    };
    for (int r = 0; r < h; ++r) {
        coefficient(1, r + 1, 1.0);
        coefficient(row_count, r + 1, -1.0);
    }
    for (int c = 0; c < w; ++c) {
        coefficient(2, h + c + 1, 1.0);
        coefficient(column_count, h + c + 1, 1.0);
    }
    for (int k = 0; k < ne; ++k) {
        glp_set_row_bnds(mip, 3 + k, GLP_UP, 0.0, 0.0);
        glp_set_row_bnds(mip, 3 + ne + k, GLP_UP, 0.0, 0.0);
        glp_set_row_bnds(mip, 3 + 2 * ne + k, GLP_UP, 0.0, 1.0);
        coefficient(3 + k, h + w + k + 1, 1.0);
        coefficient(3 + k, empty_pos[k].first + 1, -1.0);
        coefficient(3 + ne + k, h + w + k + 1, 1.0);
        coefficient(3 + ne + k, h + empty_pos[k].second + 1, -1.0);
        coefficient(3 + 2 * ne + k, empty_pos[k].first + 1, 1.0);
        coefficient(3 + 2 * ne + k, h + empty_pos[k].second + 1, 1.0);
        coefficient(3 + 2 * ne + k, h + w + k + 1, -1.0);
    }
    for (int b = 0; b <= m_c; ++b) {
        coefficient(one_hot, mu_start + b, 1.0);
        coefficient(column_count, mu_start + b, -b);
        coefficient(row_count, xi_start + b, 1.0);
        glp_set_row_bnds(mip, xi_bound + b, GLP_UP, 0.0, 0.0);
        coefficient(xi_bound + b, xi_start + b, 1.0);
        coefficient(xi_bound + b, mu_start + b, -m_r);
    }
    glp_load_matrix(mip, ar.size() - 1, ia.data(), ja.data(), ar.data());

    // Step 1: Solve the LP relaxation to obtain the initial basis for branch and bound.
    glp_smcp sparm;
    glp_init_smcp(&sparm);
    sparm.msg_lev = GLP_MSG_OFF;
    int lp_ret = glp_simplex(mip, &sparm);
    if (lp_ret != 0 || glp_get_status(mip) != GLP_OPT)
    {
        std::fprintf(stderr, "GLPK: LP relaxation failed (ret=%d, status=%d)\n",
                     lp_ret, glp_get_status(mip));
        std::exit(1);
    }

    // Step 2: Solve the MIP exactly, with no time limit.
    glp_iocp iparm;
    glp_init_iocp(&iparm);
    iparm.msg_lev = GLP_MSG_OFF;
    iparm.presolve = GLP_OFF; // Reuse the LP basis.

    int mip_ret = glp_intopt(mip, &iparm);
    int mip_stat = glp_mip_status(mip);
    if (mip_ret != 0 || mip_stat != GLP_OPT)
    {
        std::fprintf(stderr, "GLPK: B&B failed (ret=%d, status=%d)\n",
                     mip_ret, mip_stat);
        std::exit(1);
    }

    ReloadParams params;
    for (int r = 0; r < h; r++)
        if (glp_mip_col_val(mip, r + 1) > 0.5)
            params.R.insert(r);
    for (int c = 0; c < w; c++)
        if (glp_mip_col_val(mip, h + c + 1) > 0.5)
            params.C.insert(c);
    ++s_bb_ok;

    glp_delete_prob(mip);

    (void)t; // Unused parameter, retained for future iteration-dependent extensions.
    return params;
}
