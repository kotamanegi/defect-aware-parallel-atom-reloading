import json
from pathlib import Path
import tempfile
import unittest

from atom_reloading_analysis import plot_metrics, trial_stats


class TimeoutResultsTest(unittest.TestCase):
    def test_plan_only_timing_and_mixed_scopes(self):
        root = Path(__file__).resolve().parents[2]
        data = {
            "schema_version": 4,
            "timing_clock": "steady_clock_wall",
            "runtime_time_scope": "sum of plan calls per trial including burn-in",
            "num_iterations": 100,
            "results": [{"environment": "Low noise", "n": 1, "runtime_ms": 2,
                         "runtime_ms_std": 0.5, "max_planning_wall_ms": 0.03}],
        }
        with tempfile.TemporaryDirectory(dir=root / "obj") as directory:
            directory = Path(directory)
            path = directory / "results_baseline.json"
            path.write_text(json.dumps(data))
            timing = plot_metrics.resolve_timing(directory)
            self.assertEqual(timing, "wall_plan")
            row = plot_metrics.load(directory, path.name, timing)["Low noise"][0]
            self.assertEqual(row["runtime_per_op_ms"], 0.02)
            self.assertEqual(row["runtime_per_op_ms_std"], 0.005)
            self.assertEqual(row[plot_metrics.PLANNING_KEY[timing]], 0.03)
            old = dict(data, schema_version=3)
            old.pop("runtime_time_scope")
            (directory / "results_proposed.json").write_text(json.dumps(old))
            with self.assertRaisesRegex(RuntimeError, "Mixed timing methods or scopes"):
                plot_metrics.resolve_timing(directory)

    def test_invalid_plan_only_metadata_is_rejected(self):
        with self.assertRaises(RuntimeError):
            plot_metrics.timing_class_of({"schema_version": 4, "timing_clock": "steady_clock_wall"})

    def test_partial_and_empty_timeout_rows_are_excluded(self):
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory(dir=root / "obj") as directory:
            path = Path(directory) / "results_baseline.json"
            rows = [
                {"environment": "Low noise", "n": 2, "runtime_ms": 10},  # Legacy format.
                {"environment": "Low noise", "n": 3, "runtime_ms": 20, "status": "completed"},
                {"environment": "Low noise", "n": 4, "runtime_ms": 30, "status": "timeout",
                 "trial_filling_rates": [0.9]},  # Incomplete condition, even if some trials completed.
                {"environment": "High noise", "n": 2, "runtime_ms": None, "status": "timeout",
                 "trial_filling_rates": []},
            ]
            path.write_text(json.dumps({"num_iterations": 100, "results": rows}))
            plotted = plot_metrics.load(Path(directory), path.name, "wall")
            self.assertEqual([row["n"] for row in plotted["Low noise"]], [2, 3])
            self.assertNotIn("High noise", plotted)
            compared = trial_stats.load(Path(directory), path.name)
            self.assertEqual(set(compared), {("Low noise", 2), ("Low noise", 3)})


if __name__ == "__main__":
    unittest.main()
