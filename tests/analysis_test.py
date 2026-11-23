"""Unit and regression tests for the Python analysis package (stdlib only)."""

from __future__ import annotations

import json
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ANALYSIS_SRC = ROOT / "analysis" / "src"
if str(ANALYSIS_SRC) not in sys.path:
    sys.path.insert(0, str(ANALYSIS_SRC))

from atom_reloading_analysis.environments import normalize_environment  # noqa: E402
from atom_reloading_analysis.filling_rate_table import (  # noqa: E402
    format_cells,
    generate_table,
)
from atom_reloading_analysis.results import (  # noqa: E402
    METHOD_SPECS,
    PLANNING_TIME_SCOPE,
    RUNTIME_TIME_SCOPE,
    SWEEP_DIR,
    load_and_verify_loss_sweep,
    load_completed_rows,
    method_files,
    resolve_timing,
    timing_class_of,
)


def make_schema4_payload(algorithm: str, rows: list[dict], **overrides) -> dict:
    payload = {
        "algorithm": algorithm,
        "schema_version": 4,
        "burn_in_operations": 20,
        "baseline_index_policy": "paper_one_based_sites",
        "timing_clock": "steady_clock_wall",
        "planning_time_scope": PLANNING_TIME_SCOPE,
        "runtime_time_scope": RUNTIME_TIME_SCOPE,
        "num_trials": 100,
        "num_iterations": 100,
        "results": rows,
    }
    payload.update(overrides)
    return payload


def make_completed_row(
    env: str = "Low noise",
    n: int = 3,
    filling_rate_percent: float = 99.1234,
    filling_rate_percent_std: float = 0.0567,
    runtime_ms: float = 123.4,
    runtime_ms_std: float = 4.5,
    max_planning_wall_ms: float = 2.345,
) -> dict:
    return {
        "environment": env,
        "status": "completed",
        "n": n,
        "num_trials_done": 100,
        "filling_rate": filling_rate_percent / 100.0,
        "filling_rate_percent": filling_rate_percent,
        "filling_rate_percent_std": filling_rate_percent_std,
        "runtime_ms": runtime_ms,
        "runtime_ms_std": runtime_ms_std,
        "max_planning_wall_ms": max_planning_wall_ms,
    }


class EnvironmentsTest(unittest.TestCase):
    def test_normalize_environment(self) -> None:
        self.assertEqual(normalize_environment("低ノイズ"), "Low noise")
        self.assertEqual(normalize_environment("高ノイズ"), "High noise")
        self.assertEqual(normalize_environment("Low noise"), "Low noise")
        self.assertEqual(normalize_environment("High noise"), "High noise")
        self.assertEqual(normalize_environment("Custom"), "Custom")


class ResultsHelpersTest(unittest.TestCase):
    def test_method_files_preserves_order_and_labels(self) -> None:
        files = method_files(("baseline", "simple_greedy", "proposed"))
        self.assertEqual(
            list(files.items()),
            [
                ("Baseline", "results_baseline.json"),
                ("Simple greedy", "results_simple_greedy.json"),
                ("Proposed", "results_proposed.json"),
            ],
        )

    def test_timing_class_of(self) -> None:
        self.assertEqual(timing_class_of(make_schema4_payload("proposed", [])), "wall_plan")
        self.assertEqual(
            timing_class_of({"schema_version": 3, "timing_clock": "steady_clock_wall"}),
            "wall",
        )
        self.assertEqual(timing_class_of({}), "legacy")
        with self.assertRaises(RuntimeError):
            timing_class_of(make_schema4_payload("proposed", [], timing_clock="cpu"))
        with self.assertRaises(RuntimeError):
            timing_class_of(make_schema4_payload("proposed", [], runtime_time_scope="other"))

    def test_resolve_timing_checks_consistency(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "a.json").write_text(json.dumps(make_schema4_payload("baseline", [])))
            (root / "b.json").write_text(json.dumps(make_schema4_payload("proposed", [])))
            self.assertEqual(resolve_timing(root, ("a.json", "b.json", "missing.json")), "wall_plan")

            # Mixed timing scope must fail.
            (root / "c.json").write_text(
                json.dumps(
                    {
                        "schema_version": 3,
                        "timing_clock": "steady_clock_wall",
                        "burn_in_operations": 20,
                        "baseline_index_policy": "paper_one_based_sites",
                    }
                )
            )
            with self.assertRaises(RuntimeError):
                resolve_timing(root, ("a.json", "c.json"))

            # Mixed burn-in convention must fail.
            (root / "d.json").write_text(
                json.dumps(make_schema4_payload("greedy", [], burn_in_operations=10))
            )
            with self.assertRaises(RuntimeError):
                resolve_timing(root, ("a.json", "d.json"))

    def test_load_completed_rows_filters_and_normalizes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.assertEqual(load_completed_rows(root / "nonexistent.json"), [])
            path = root / "results_proposed.json"
            payload = make_schema4_payload(
                "proposed",
                [
                    make_completed_row(env="低ノイズ", n=2, runtime_ms=250.0, runtime_ms_std=10.0),
                    {"environment": "高ノイズ", "status": "timeout", "n": 3, "runtime_ms": None},
                ],
            )
            path.write_text(json.dumps(payload))
            rows = load_completed_rows(path)
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["environment"], "Low noise")
            self.assertAlmostEqual(rows[0]["runtime_per_op_ms"], 2.5)
            self.assertAlmostEqual(rows[0]["runtime_per_op_ms_std"], 0.1)


class FillingRateTableTest(unittest.TestCase):
    def test_format_cells_and_errors(self) -> None:
        data = make_schema4_payload(
            "proposed",
            [
                make_completed_row(
                    env="低ノイズ",
                    n=3,
                    filling_rate_percent=99.1234,
                    filling_rate_percent_std=0.0567,
                    runtime_ms=123.4,
                    max_planning_wall_ms=2.345,
                ),
                {"environment": "High noise", "status": "timeout", "n": 3},
            ],
        )
        self.assertEqual(
            format_cells(data, "Low noise", 3),
            "99.1234 & 0.0567 & 1.234 & 2.345",
        )
        self.assertEqual(
            format_cells(data, "High noise", 3),
            r"\multicolumn{2}{c}{---} & \multicolumn{1}{c}{---} & \multicolumn{1}{c}{---}",
        )
        with self.assertRaises(ValueError):
            format_cells(
                make_schema4_payload("proposed", [make_completed_row(), make_completed_row()]),
                "Low noise",
                3,
            )
        with self.assertRaises(ValueError):
            format_cells(
                make_schema4_payload("proposed", [make_completed_row()], planning_time_scope="bad"),
                "Low noise",
                3,
            )

    def test_generate_table_and_wrapper_cli(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            results_dir = Path(tmp)
            for key in ("baseline", "proposed", "greedy", "simple_greedy", "mip_glpk", "mip_scip"):
                payload = make_schema4_payload(
                    key,
                    [
                        make_completed_row(env="Low noise", n=3),
                        make_completed_row(env="High noise", n=3),
                    ],
                )
                (results_dir / METHOD_SPECS[key].filename).write_text(json.dumps(payload))

            row_by_row_path = results_dir / "row_by_row.json"
            row_by_row_path.write_text(
                json.dumps(
                    make_schema4_payload(
                        "row_by_row",
                        [
                            make_completed_row(env="Low noise", n=3, runtime_ms=1.2345, max_planning_wall_ms=0.0456),
                            make_completed_row(env="High noise", n=3, runtime_ms=2.3456, max_planning_wall_ms=0.0567),
                        ],
                    )
                )
            )
            table = generate_table(results_dir, n=3, row_by_row_results=row_by_row_path)
            self.assertIn(r"\begin{tabular}", table)
            self.assertIn("Simple greedy & 99.1234 & 0.0567 & 1.234 & 2.345", table)
            self.assertIn("Row-by-row & 99.1234 & 0.0567 & 0.0123 & 0.0456", table)

            proc = subprocess.run(
                [
                    sys.executable,
                    str(ROOT / "analysis" / "filling_rate_table.py"),
                    "--results-dir",
                    str(results_dir),
                    "--n",
                    "3",
                    "--row-by-row-results",
                    str(row_by_row_path),
                ],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(proc.stdout, table)


class LossSweepVerificationTest(unittest.TestCase):
    def test_synthetic_verification_and_error_detection(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            sweep_dir = Path(tmp)
            values = [0.99, 0.98, 0.995]
            summary = {
                "trials": len(values),
                "results": [
                    {
                        "p_idle": 0.004,
                        "p_op": 0.004,
                        "mean": statistics.mean(values),
                        "sample_sd": statistics.stdev(values),
                    }
                ],
            }
            csv_lines = ["p_idle,p_op,trial,filling_rate"] + [
                f"0.004,0.004,{i},{v}" for i, v in enumerate(values)
            ]
            (sweep_dir / "summary.json").write_text(json.dumps(summary))
            (sweep_dir / "trials.csv").write_text("\n".join(csv_lines) + "\n")

            verified = load_and_verify_loss_sweep(sweep_dir)
            self.assertEqual(len(verified), 1)

            # Corrupt summary mean -> must fail.
            bad_summary = json.loads(json.dumps(summary))
            bad_summary["results"][0]["mean"] += 1e-6
            (sweep_dir / "summary.json").write_text(json.dumps(bad_summary))
            with self.assertRaises(ValueError):
                load_and_verify_loss_sweep(sweep_dir)

            # Out-of-range filling rate -> must fail.
            (sweep_dir / "summary.json").write_text(json.dumps(summary))
            (sweep_dir / "trials.csv").write_text(
                "p_idle,p_op,trial,filling_rate\n0.004,0.004,0,1.1\n0.004,0.004,1,0.9\n0.004,0.004,2,0.9\n"
            )
            with self.assertRaises(ValueError):
                load_and_verify_loss_sweep(sweep_dir)

    def test_archived_sweep_verifies_when_present(self) -> None:
        archived = ROOT / "results" / SWEEP_DIR
        if archived.is_dir():
            results = load_and_verify_loss_sweep(archived)
            self.assertEqual(len(results), 21)


if __name__ == "__main__":
    unittest.main()
