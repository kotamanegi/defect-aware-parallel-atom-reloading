"""Per-metric figures for rectangular-zone experiments.
Geometry: storage 12n x 30n, preparation 4n x 15n, n = 1..15.

Reads results/results_*.json and writes to figures/:
  filling_rate_{low,high}_noise.pdf
  runtime_{low,high}_noise.pdf
  max_planning_time_{low,high}_noise.pdf
  convergence_iterations.pdf
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from .environments import normalize_environment  # noqa: E402
from .paths import add_path_arguments, resolve_dirs  # noqa: E402

RCPARAMS = {
    "font.size": 12,
    "axes.labelsize": 12,
    "legend.fontsize": 9,
    "figure.dpi": 150,
}

FILES = {
    "Baseline":       "results_baseline.json",
    "Greedy":         "results_greedy.json",
    "Simple greedy":  "results_simple_greedy.json",
    "Proposed":       "results_proposed.json",
    "MIP (SCIP)":     "results_mip_scip.json",
    "MIP (GLPK)":     "results_mip_glpk.json",
}

STYLE = {
    "Baseline":       dict(color="#7f7f7f", linestyle=":",  marker="x"),
    "Greedy":         dict(color="#9467bd", linestyle=":",  marker="s"),
    "Simple greedy":  dict(color="#bcbd22", linestyle=":",  marker="P"),
    "Proposed":       dict(color="#2ca02c", linestyle="-",  marker="o"),
    "MIP (SCIP)":     dict(color="#d62728", linestyle="--", marker="D"),
    "MIP (GLPK)":     dict(color="#1f77b4", linestyle="-.", marker="^"),
}

ENVS = {"Low noise": "low_noise", "High noise": "high_noise"}
XLABEL = r"Scale factor $n$ (storage $12n \times 30n$)"

# Use planning-time keys appropriate to each measurement method.
#   legacy: data without schema_version (CPU time from std::clock()).
#   wall:   schema_version=2/3 (mean time includes simulator work).
#   wall_plan: schema_version=4 (mean and maximum cover planning calls only).
PLANNING_KEY = {"legacy": "max_planning_time_ms", "wall": "max_planning_wall_ms",
                "wall_plan": "max_planning_wall_ms"}
TIMING_NOTE = {
    "legacy": "(CPU clock)",
    "wall": "",
    "wall_plan": "",
}


def timing_class_of(d: dict) -> str:
    if d.get("schema_version") == 4:
        if (d.get("timing_clock") != "steady_clock_wall" or
                d.get("runtime_time_scope") != "sum of plan calls per trial including burn-in"):
            raise RuntimeError("Invalid timing metadata for schema_version=4.")
        return "wall_plan"
    if d.get("schema_version") in (2, 3) and "timing_clock" in d:
        return "wall"
    return "legacy"


# Determine the timing method across result files; reject mixed methods to avoid combining
# CPU time, wall time including simulator work, and planning-only wall time.
def resolve_timing(results_dir: Path) -> str:
    classes = set()
    averaging = set()
    for fname in FILES.values():
        path = results_dir / fname
        if not path.exists():
            continue
        with open(path) as f:
            data = json.load(f)
            classes.add(timing_class_of(data))
            averaging.add((data.get("burn_in_operations", 10),
                           data.get("baseline_index_policy", "legacy_zero_based_sites")))
    if len(averaging) > 1:
        raise RuntimeError("Results use incompatible burn-in or indexing conventions.")
    if len(classes) > 1:
        raise RuntimeError(
            "Mixed timing methods or scopes (CPU time, simulator-inclusive, planning-only)."
            " Use the same timing method and scope for all files in results/."
        )
    return classes.pop() if classes else "legacy"


def load(results_dir: Path, fname: str, timing: str):
    path = results_dir / fname
    if not path.exists():
        return {}
    with open(path) as f:
        d = json.load(f)
    T = d["num_iterations"]
    rows = {}
    for r in d["results"]:
        if r.get("status", "completed") != "completed":
            continue
        r["runtime_per_op_ms"] = r["runtime_ms"] / T
        if "runtime_ms_std" in r:
            r["runtime_per_op_ms_std"] = r["runtime_ms_std"] / T
        r["environment"] = normalize_environment(r["environment"])
        rows.setdefault(r["environment"], []).append(r)
    for env in rows:
        rows[env].sort(key=lambda r: r["n"])
    return rows


def series_err(rows, env, key, err_key):
    pts = [(r["n"], r[key], r.get(err_key, 0.0)) for r in rows.get(env, []) if key in r]
    return [p[0] for p in pts], [p[1] for p in pts], [p[2] for p in pts]


def new_ax():
    fig, ax = plt.subplots(figsize=(5.5, 4.0))
    ax.grid(True, alpha=0.3)
    ax.set_xlabel(XLABEL)
    return fig, ax


def save(fig, figures_dir: Path, name: str):
    figures_dir.mkdir(parents=True, exist_ok=True)
    path = figures_dir / name
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print("Saved:", path)


def plot_filling_rate(data, figures_dir: Path) -> None:
    for environment, file_suffix in ENVS.items():
        fig, ax = new_ax()
        for label, rows in data.items():
            if not rows:
                continue
            ns, ys, es = series_err(
                rows, environment, "filling_rate_percent", "filling_rate_percent_std"
            )
            if ns:
                ax.errorbar(ns, ys, yerr=es, linewidth=1.8, markersize=5,
                            capsize=3, elinewidth=1.0, label=label, **STYLE[label])
        ax.set_ylabel("Filling rate (%)")
        ax.set_ylim(top=100)
        # The low-noise curves leave a gap above Baseline on the left;
        # the high-noise curves leave the upper-right corner clear.
        legend_loc = "center left" if file_suffix == "low_noise" else "upper right"
        ax.legend(loc=legend_loc)
        save(fig, figures_dir, f"filling_rate_{file_suffix}.pdf")


def plot_runtime(data, figures_dir: Path, timing: str) -> None:
    note = TIMING_NOTE[timing]
    for environment, file_suffix in ENVS.items():
        fig, ax = new_ax()
        for label, rows in data.items():
            if not rows:
                continue
            ns, ys, es = series_err(
                rows, environment, "runtime_per_op_ms", "runtime_per_op_ms_std"
            )
            if ns:
                ax.errorbar(ns, ys, yerr=es, linewidth=1.8, markersize=5,
                            capsize=3, elinewidth=1.0, label=label, **STYLE[label])
        label = "Mean planning time" if timing == "wall_plan" else "Runtime per reload operation"
        ax.set_ylabel(f"{label} (ms) {note}".rstrip())
        ax.set_yscale("log")
        # Use the gap between the MIP and heuristic planning times.
        if file_suffix == "low_noise":
            ax.legend(loc="center right", bbox_to_anchor=(1, 0.63), ncol=2)
        else:
            ax.legend(loc="upper right", ncol=2)
        save(fig, figures_dir, f"runtime_{file_suffix}.pdf")


def plot_max_planning_time(data, figures_dir: Path, timing: str) -> None:
    # No error bars: max_planning_* is already a maximum statistic.
    key = PLANNING_KEY[timing]
    note = TIMING_NOTE[timing]
    for environment, file_suffix in ENVS.items():
        fig, ax = new_ax()
        for label, rows in data.items():
            if not rows:
                continue
            pts = [(r["n"], r[key]) for r in rows.get(environment, [])
                   if key in r and r[key] > 0]
            if pts:
                ns = [p[0] for p in pts]
                ys = [p[1] for p in pts]
                ax.plot(ns, ys, linewidth=1.8, markersize=5, label=label, **STYLE[label])
        ax.set_ylabel(f"Max single planning time (ms) {note}".rstrip())
        ax.set_yscale("log")
        ax.legend(loc="center right", bbox_to_anchor=(1, 0.55), ncol=2)
        save(fig, figures_dir, f"max_planning_time_{file_suffix}.pdf")


def plot_convergence(data, figures_dir: Path) -> None:
    fig, ax = new_ax()
    noise_style = {
        "Low noise": dict(color="#2ca02c", linestyle="-", marker="o",
                     label=r"Proposed ($p = 0.004$)"),
        "High noise": dict(color="#ff7f0e", linestyle="--", marker="s",
                     label=r"Proposed ($p = 0.020$)"),
    }
    for environment, st in noise_style.items():
        if "Proposed" in data:
            pts = [(r["n"], r["avg_convergence_iterations"])
                   for r in data["Proposed"].get(environment, [])
                   if "avg_convergence_iterations" in r
                   and r["avg_convergence_iterations"] >= 0]
            if pts:
                ns = [p[0] for p in pts]
                ys = [p[1] for p in pts]
                ax.plot(ns, ys, linewidth=1.8, markersize=5, **st)
    ax.set_ylabel("Iterations until convergence")
    ax.set_ylim(0, 10)
    ax.set_axisbelow(True)
    ax.grid(True, axis="both", which="major", color="0.75", linewidth=0.8, alpha=1.0)
    ax.legend(loc="upper left")
    save(fig, figures_dir, "convergence_iterations.pdf")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_path_arguments(parser)
    args = parser.parse_args()
    results_dir, figures_dir = resolve_dirs(args)

    plt.rcParams.update(RCPARAMS)
    timing = resolve_timing(results_dir)
    data = {label: load(results_dir, fname, timing) for label, fname in FILES.items()}

    plot_filling_rate(data, figures_dir)
    plot_runtime(data, figures_dir, timing)
    plot_max_planning_time(data, figures_dir, timing)
    plot_convergence(data, figures_dir)

    print("All plots generated.")


if __name__ == "__main__":
    main()
