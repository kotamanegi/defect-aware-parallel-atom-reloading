"""CLI / JSON integration checks. All artifacts stay below obj/."""
import json
import math
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / "atom_reloading"


def run(cwd, *args):
    return subprocess.run([str(BINARY), *args], cwd=cwd, capture_output=True,
                          text=True, timeout=30, check=True)


with tempfile.TemporaryDirectory(prefix="benchmark-test-", dir=ROOT / "obj") as tmp:
    directory = Path(tmp)
    # Use a tiny budget to interrupt the first n for each method and noise condition.
    output = run(directory, "-a", "all", "-n", "3", "-c", "0.000001")
    files = sorted((directory / "results").glob("results_*.json"))
    assert {path.stem.removeprefix("results_") for path in files} == {
        "baseline", "proposed", "mip_scip", "greedy", "simple_greedy", "mip_glpk", "row_by_row"
    }, output.stdout
    assert output.stdout.count("skipped (cutoff)") == 4 * len(files), output.stdout
    for path in files:
        data = json.loads(path.read_text())
        if data["algorithm"] == "row_by_row":
            assert data["experiment_role"] == "additional_row_by_row_comparison"
            assert "cyclic" in data["row_selection_policy"]
        assert data["cutoff_policy"] == "cumulative_plan_time_budget"
        assert data["schema_version"] == 4
        assert data["runtime_time_scope"] == "sum of plan calls per trial including burn-in"
        assert len(data["results"]) == 2
        assert {r["environment"] for r in data["results"]} == {"Low noise", "High noise"}
        for row in data["results"]:
            assert row["n"] == 1 and row["status"] == "timeout", row
            # The tiny cumulative budget (0.000001 ms x 10,000 calls = 0.01 ms)
            # forces a cutoff early in the first trial.
            # Some operations may complete before cutoff, so max_planning_wall_ms need not be null.
            assert row["num_trials_done"] == 0, row
            assert row["timeout_trial"] == 1, row
            assert row["trial_filling_rates"] == row["trial_runtimes_ms"] == [], row
            assert row["filling_rate"] is None and row["runtime_ms"] is None, row
            assert row["timeout_planning_wall_ms"] > 0, row

    # A separate run after a timeout should complete and save all trials.
    run(directory, "-a", "baseline", "-n", "1", "-H", "-c", "100",
        "-o", "completed.json")
    data = json.loads((directory / "completed.json").read_text())
    assert len(data["results"]) == 1
    row = data["results"][0]
    assert row["status"] == "completed" and row["num_trials_done"] == 100, row
    assert row["num_operations_done"] == 10000, row
    assert len(row["trial_filling_rates"]) == len(row["trial_runtimes_ms"]) == 100
    assert math.isclose(row["runtime_ms"], sum(row["trial_runtimes_ms"]) / 100, abs_tol=1e-6)
    assert 0 < row["runtime_ms"] / data["num_iterations"] <= row["max_planning_wall_ms"] + 1e-6
    assert 0 < row["filling_rate"] <= 1
    assert "timeout_trial" not in row

    for args in (["-c"], *(["-c", value] for value in
                          ["0", "-1", "nan", "inf", "1e300", "100oops", "oops"])):
        result = subprocess.run([str(BINARY), *args], cwd=directory,
                                capture_output=True, text=True, timeout=5)
        assert result.returncode != 0 and "Error:" in result.stderr, result

print("Benchmark CLI / JSON tests passed.")
