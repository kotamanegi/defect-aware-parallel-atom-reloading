"""Compatibility checks for English and historical Japanese environment labels."""

import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from atom_reloading_analysis import plot_metrics, trial_stats

ROOT = Path(__file__).resolve().parents[2]


class EnvironmentLabelsTest(unittest.TestCase):
    def test_mixed_labels_share_plot_and_statistics_conditions(self):
        # Historical input remains readable while new input uses English labels.
        rows = [
            {"environment": "低ノイズ", "n": 1, "runtime_ms": 10,
             "trial_filling_rates": [0.9, 0.95]},
            {"environment": "Low noise", "n": 2, "runtime_ms": 20,
             "trial_filling_rates": [0.91, 0.96]},
            {"environment": "高ノイズ", "n": 1, "runtime_ms": 30,
             "trial_filling_rates": [0.8, 0.85]},
            {"environment": "High noise", "n": 2, "runtime_ms": 40,
             "trial_filling_rates": [0.81, 0.86]},
            {"environment": "低ノイズ", "n": 3, "runtime_ms": None,
             "status": "timeout"},
        ]
        with tempfile.TemporaryDirectory(dir=ROOT / "obj") as tmp:
            directory = Path(tmp)
            path = directory / "results_baseline.json"
            path.write_text(json.dumps({"num_iterations": 100, "results": rows}))
            original = path.read_bytes()
            plotted = plot_metrics.load(directory, path.name, "wall_plan")
            self.assertEqual(set(plotted), {"Low noise", "High noise"})
            for environment in plotted:
                self.assertEqual([r["n"] for r in plotted[environment]], [1, 2])
                self.assertTrue(all(r["environment"] == environment for r in plotted[environment]))
            compared = trial_stats.load(directory, path.name)
            self.assertEqual(set(compared), {
                (environment, n) for environment in ("Low noise", "High noise") for n in (1, 2)
            })
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                trial_stats.print_filling_rate_table({"Baseline": compared}, sorted(compared))
            self.assertIn("Low noise n= 1:", output.getvalue())
            self.assertIn("High noise n= 2:", output.getvalue())
            self.assertNotIn("ノイズ", output.getvalue())
            self.assertEqual(path.read_bytes(), original)

    def test_comparison_table_is_identical_for_both_label_formats(self):
        # Run the standalone command with real archived data and a mixed-label copy.
        script = ROOT / "analysis/filling_rate_table.py"
        original = subprocess.run(
            [sys.executable, str(script), "--n", "3"], check=True,
            text=True, capture_output=True, cwd=ROOT,
        ).stdout
        with tempfile.TemporaryDirectory(dir=ROOT / "obj") as tmp:
            directory = Path(tmp)
            for index, path in enumerate(sorted((ROOT / "results").rglob("results_*.json"))):
                data = json.loads(path.read_text())
                if index % 2 == 0:
                    for row in data["results"]:
                        row["environment"] = {
                            "低ノイズ": "Low noise", "高ノイズ": "High noise",
                        }.get(row["environment"], row["environment"])
                target = directory / path.relative_to(ROOT / "results")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(json.dumps(data))
            translated = subprocess.run(
                [sys.executable, str(script), "--results-dir", str(directory), "--n", "3"],
                check=True, text=True, capture_output=True, cwd=directory,
            ).stdout
        self.assertEqual(translated, original)
        self.assertIn("Low-noise case", translated)
        self.assertIn("High-noise case", translated)


if __name__ == "__main__":
    unittest.main()
