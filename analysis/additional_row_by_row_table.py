"""Validate the separate row-by-row experiment and generate its table and CSV.

Uses only the standard library; does not modify or regenerate any figure.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
from statistics import mean, stdev


def validate(data):
    assert data["algorithm"] == "row_by_row"
    assert data["experiment_role"] == "additional_row_by_row_comparison"
    assert data["num_trials"] == 100 and data["num_iterations"] == 100
    assert data["burn_in_operations"] == 20
    assert data["trial_indices"] == list(range(100))
    assert data["seed_base"] == 998244353
    assert data["rng_policy"] == "site_noise_v1_mt19937_seed_seq_uniform53"
    assert data["runtime_time_scope"] == "sum of plan calls per trial including burn-in"
    rows = data["results"]
    expected = {(n, p) for n in range(1, 16) for p in (0.004, 0.02)}
    assert len(rows) == 30 and {(r["n"], r["p_idle"]) for r in rows} == expected
    for r in rows:
        assert r["status"] == "completed" and r["num_trials_done"] == 100
        assert r["num_operations_done"] == 10000
        n = r["n"]
        assert (r["h"], r["w"], r["m_r"], r["m_c"]) == (12*n, 30*n, 4*n, 15*n)
        assert r["p_reload"] == r["p_idle"]
        rates, times = r["trial_filling_rates"], r["trial_runtimes_ms"]
        assert len(rates) == len(times) == 100
        assert all(0 <= f <= 1 for f in rates)
        assert all(t >= 0 for t in times)
        assert r["avg_discarded_atoms"] == 0
        assert abs(mean(rates) * 100 - r["filling_rate_percent"]) < 1e-8
        assert abs(stdev(rates) * 100 - r["filling_rate_percent_std"]) < 1e-8
        assert abs(mean(times) - r["runtime_ms"]) < 1e-8
        assert abs(stdev(times) - r["runtime_ms_std"]) < 1e-8
        assert 0 <= r["runtime_ms"] / 100 <= r["max_planning_wall_ms"]
    return {(r["n"], r["p_idle"]): r for r in rows}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("result", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    raw = args.result.read_bytes()
    data = json.loads(raw)
    rows = validate(data)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    fields = ["experiment_id", "n", "p", "filling_rate_percent", "sample_sd_percent",
              "mean_planning_ms_per_step", "sample_sd_planning_ms_per_step", "worst_plan_ms"]
    with (args.output_dir / "additional-row-by-row-summary.csv").open("w", newline="") as out:
        writer = csv.writer(out)
        writer.writerow(fields)
        for n in range(1, 16):
            for p in (0.004, 0.02):
                r = rows[n, p]
                writer.writerow(["additional_row_by_row_20260927", n, p,
                                 r["filling_rate_percent"], r["filling_rate_percent_std"],
                                 r["runtime_ms"] / 100, r["runtime_ms_std"] / 100,
                                 r["max_planning_wall_ms"]])
    tex = r"""% Generated from the additional experiment; do not edit numerical values by hand.
\begin{table}[t]
  \centering
  \caption{Additional experiment: cyclic row-by-row reloading, with $100$ trials per setting. Filling rates $F$ are means $\pm$ sample standard deviations over steps $21$--$100$ from a fully occupied initial state, and include initial-state transients. Planning times are mean wall times per step over all $100$ steps, including the first $20$ steps, with simulator overhead excluded. These results are not included in the figures.}\label{table:additional_row_by_row}
  \setlength{\tabcolsep}{5pt}
  \begin{tabular}{rcccc}
    \toprule
    & \multicolumn{2}{c}{$F$ (\%)} & \multicolumn{2}{c}{Time/step (ms)} \\
    $n$ & $p=0.004$ & $p=0.020$ & $p=0.004$ & $p=0.020$ \\
    \midrule
"""
    for n in range(1, 16):
        lo, hi = rows[n, 0.004], rows[n, 0.02]
        tex += (f'    {n} & ${lo["filling_rate_percent"]:.4f} \\pm '
                f'{lo["filling_rate_percent_std"]:.4f}$ & '
                f'${hi["filling_rate_percent"]:.4f} \\pm {hi["filling_rate_percent_std"]:.4f}$ & '
                f'${lo["runtime_ms"]/100:.4f}$ & ${hi["runtime_ms"]/100:.4f}$ '
                + r"\\" + "\n")
    tex += r"""    \botrule
  \end{tabular}
\end{table}
"""
    (args.output_dir / "additional-row-by-row-table.tex").write_text(tex)
    validation = {
        "experiment_id": "additional_row_by_row_20260927",
        "result_sha256": hashlib.sha256(raw).hexdigest(),
        "simulation_commit": data["commit_id"],
        "completed_settings": 30,
        "completed_trials": 3000,
        "completed_operations": 300000,
        "checks": ["all settings complete", "main-experiment seed and geometry",
                   "mean and sample SD from trial data", "planning-time consistency",
                   "no occupied atoms reloaded"],
    }
    (args.output_dir / "validation.json").write_text(json.dumps(validation, indent=2) + "\n")
    print(json.dumps(validation, indent=2))


if __name__ == "__main__":
    main()
