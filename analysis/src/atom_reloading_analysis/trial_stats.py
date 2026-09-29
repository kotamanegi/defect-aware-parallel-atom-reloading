"""Per-trial statistics for the rectangular-zone experiments, with Welch's t-test.

Reads results/results_*.json.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

from scipy import stats

from .environments import normalize_environment
from .paths import add_path_arguments, resolve_dirs

FILES = {
    "Baseline":      "results_baseline.json",
    "Greedy":        "results_greedy.json",
    "Simple Greedy": "results_simple_greedy.json",
    "Proposed":      "results_proposed.json",
    "MIP (SCIP)":    "results_mip_scip.json",
}

PAIRS = [
    ("Proposed", "Greedy"),
    ("Proposed", "Baseline"),
    ("Proposed", "Simple Greedy"),
    ("MIP (SCIP)", "Proposed"),
]


def load(results_dir: Path, fname: str):
    path = results_dir / fname
    if not path.exists():
        return {}
    with open(path) as f:
        d = json.load(f)
    rows = {}
    for r in d["results"]:
        if r.get("status", "completed") != "completed":
            continue
        r["environment"] = normalize_environment(r["environment"])
        rows[(r["environment"], r["n"])] = r
    return rows


def ci95(xs):
    n = len(xs)
    mean = sum(xs) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in xs) / (n - 1))
    half = stats.t.ppf(0.975, n - 1) * sd / math.sqrt(n)
    return mean, sd, half


def print_filling_rate_table(data, conditions) -> None:
    print("=== Filling rate (%): mean / SD / 95% CI half-width, per 100 trials ===")
    for env, n in conditions:
        line = f"{env} n={n:2d}: "
        parts = []
        for label, rows in data.items():
            r = rows.get((env, n))
            if r is None or "trial_filling_rates" not in r:
                continue
            xs = [x * 100 for x in r["trial_filling_rates"]]
            mean, sd, half = ci95(xs)
            parts.append(f"{label}={mean:.4f} (SD {sd:.4f}, CI +/-{half:.4f})")
        print(line + "; ".join(parts))


def print_welch_tests(data, conditions) -> None:
    print("=== Welch's t-test on filling rate (per condition) ===")
    for a, b in PAIRS:
        print(f"--- {a} vs {b} ---")
        for env, n in conditions:
            ra, rb = data[a].get((env, n)), data[b].get((env, n))
            if not ra or not rb:
                continue
            if "trial_filling_rates" not in ra or "trial_filling_rates" not in rb:
                continue
            xa = [x * 100 for x in ra["trial_filling_rates"]]
            xb = [x * 100 for x in rb["trial_filling_rates"]]
            t, p = stats.ttest_ind(xa, xb, equal_var=False)
            diff = sum(xa) / len(xa) - sum(xb) / len(xb)
            sig = (
                "" if p >= 0.05
                else " ***" if p < 0.001
                else " **" if p < 0.01
                else " *" if p < 0.05
                else ""
            )
            print(f"  {env} n={n:2d}: diff={diff:+.4f} pt, t={t:+8.2f}, p={p:.2e}{sig}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_path_arguments(parser)
    args = parser.parse_args()
    results_dir, _ = resolve_dirs(args)

    data = {label: load(results_dir, fname) for label, fname in FILES.items()}
    conditions = sorted(
        {k for rows in data.values() for k in rows},
        key=lambda k: (k[0], k[1]),
    )

    print_filling_rate_table(data, conditions)
    print()
    print_welch_tests(data, conditions)


if __name__ == "__main__":
    main()
