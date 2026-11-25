"""Shared method metadata, result JSON loading, and validation helpers.

Uses only the Python standard library so standalone scripts (such as
``analysis/filling_rate_table.py``) can import this module without external
dependencies.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
import json
import math
from pathlib import Path
import statistics
from typing import Iterable

from .environments import normalize_environment

SWEEP_DIR = "independent_loss_sweep_20261108"
ROW_BY_ROW_RESULTS = Path("additional_row_by_row_20260927/results_row_by_row.json")

PLANNING_TIME_SCOPE = "plan call only"
RUNTIME_TIME_SCOPE = "sum of plan calls per trial including burn-in"


@dataclass(frozen=True)
class MethodSpec:
    key: str
    label: str
    filename: str
    latex_label: str


METHOD_SPECS: dict[str, MethodSpec] = {
    "baseline": MethodSpec(
        "baseline", "Baseline", "results_baseline.json", r"Baseline~\cite{cite:3000_coherent}"
    ),
    "proposed": MethodSpec("proposed", "Proposed", "results_proposed.json", "Proposed"),
    "greedy": MethodSpec("greedy", "Greedy", "results_greedy.json", "Greedy"),
    "simple_greedy": MethodSpec(
        "simple_greedy", "Simple greedy", "results_simple_greedy.json", "Simple greedy"
    ),
    "mip_glpk": MethodSpec("mip_glpk", "MIP (GLPK)", "results_mip_glpk.json", "MIP (GLPK)"),
    "mip_scip": MethodSpec("mip_scip", "MIP (SCIP)", "results_mip_scip.json", "MIP (SCIP)"),
    "row_by_row": MethodSpec(
        "row_by_row", "Row-by-row", "results_row_by_row.json", "Row-by-row"
    ),
}


def method_files(keys: Iterable[str]) -> dict[str, str]:
    """Return an ordered ``{display_label: filename}`` mapping for method keys."""
    return {METHOD_SPECS[key].label: METHOD_SPECS[key].filename for key in keys}


def timing_class_of(data: dict) -> str:
    """Classify the timing convention recorded in a benchmark JSON payload."""
    if data.get("schema_version") == 4:
        if (
            data.get("timing_clock") != "steady_clock_wall"
            or data.get("runtime_time_scope") != RUNTIME_TIME_SCOPE
        ):
            raise RuntimeError("Invalid timing metadata for schema_version=4.")
        return "wall_plan"
    if data.get("schema_version") in (2, 3) and "timing_clock" in data:
        return "wall"
    return "legacy"


def resolve_timing(results_dir: Path, filenames: Iterable[str]) -> str:
    """Resolve the shared timing class across result files, rejecting mismatches."""
    classes = set()
    averaging = set()
    for fname in filenames:
        path = results_dir / fname
        if not path.exists():
            continue
        with open(path) as f:
            data = json.load(f)
        classes.add(timing_class_of(data))
        averaging.add(
            (
                data.get("burn_in_operations", 10),
                data.get("baseline_index_policy", "legacy_zero_based_sites"),
            )
        )
    if len(averaging) > 1:
        raise RuntimeError("Results use incompatible burn-in or indexing conventions.")
    if len(classes) > 1:
        raise RuntimeError(
            "Mixed timing methods or scopes (CPU time, simulator-inclusive, planning-only)."
            " Use the same timing method and scope for all files in results/."
        )
    return classes.pop() if classes else "legacy"


def load_completed_rows(path: Path) -> list[dict]:
    """Load completed benchmark rows with normalized environments and per-op times."""
    if not path.exists():
        return []
    with open(path) as f:
        data = json.load(f)
    num_iterations = data["num_iterations"]
    completed: list[dict] = []
    for row in data["results"]:
        if row.get("status", "completed") != "completed":
            continue
        entry = dict(row)
        entry["environment"] = normalize_environment(entry["environment"])
        if "runtime_ms" in entry and entry["runtime_ms"] is not None:
            entry["runtime_per_op_ms"] = entry["runtime_ms"] / num_iterations
        if "runtime_ms_std" in entry and entry["runtime_ms_std"] is not None:
            entry["runtime_per_op_ms_std"] = entry["runtime_ms_std"] / num_iterations
        completed.append(entry)
    return completed


def load_and_verify_loss_sweep(sweep_dir: Path) -> list[dict]:
    """Return loss-sweep summary rows after verifying them against ``trials.csv``."""
    summary = json.loads((sweep_dir / "summary.json").read_text())
    with (sweep_dir / "trials.csv").open(newline="") as source:
        rows = list(csv.DictReader(source))

    groups: dict[tuple[float, float], list[dict]] = {}
    for row in rows:
        key = float(row["p_idle"]), float(row["p_op"])
        groups.setdefault(key, []).append(row)
    results = summary["results"]
    if len(results) != len(groups):
        raise ValueError("Raw trials and summary have different conditions")
    for result in results:
        group = groups[(result["p_idle"], result["p_op"])]
        if len(group) != summary["trials"] or {
            int(row["trial"]) for row in group
        } != set(range(summary["trials"])):
            raise ValueError("Incomplete or duplicate trial indices")
        values = [float(row["filling_rate"]) for row in group]
        if not all(0 <= value <= 1 for value in values):
            raise ValueError("Filling rates must be between zero and one")
        for recorded, calculated in (
            (result["mean"], statistics.mean(values)),
            (result["sample_sd"], statistics.stdev(values)),
        ):
            if not math.isclose(recorded, calculated, rel_tol=0, abs_tol=1e-12):
                raise ValueError("Summary statistics disagree with raw trials")
    return results
