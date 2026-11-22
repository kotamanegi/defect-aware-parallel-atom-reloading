#!/usr/bin/env python3
"""Run the independent loss-probability sweep and summarize the trials.

The script builds the simulator with `make`, links driver.cpp against the
common simulator objects (no simulator source changes), runs the sweep, and
writes trials.csv and summary.json to the output directory.  The default output
directory is experiments/independent_loss_sweep/output/, so the archived data in
results/independent_loss_sweep_20261108/ are never overwritten.
"""

import argparse
import csv
import hashlib
import json
import pathlib
import statistics
import subprocess

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[1]
ARCHIVE = REPO / "results" / "independent_loss_sweep_20261108"

# These values must match the loops in driver.cpp.
P_IDLE = (0, 0.002, 0.004, 0.008, 0.012, 0.016, 0.020)
P_OP = (0, 0.004, 0.020)
TRIALS = 100


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--output-dir",
        type=pathlib.Path,
        default=HERE / "output",
        help="Directory for the driver binary, build log, trials.csv and summary.json "
        "(default: experiments/independent_loss_sweep/output)",
    )
    parser.add_argument(
        "--jobs", type=int, default=4, help="Parallel jobs for make (default: 4)"
    )
    args = parser.parse_args()
    out = args.output_dir.resolve()
    if out == ARCHIVE.resolve():
        raise SystemExit(f"Refusing to overwrite the archived results in {ARCHIVE}")
    out.mkdir(parents=True, exist_ok=True)

    with (out / "build.log").open("w") as log:
        subprocess.run(
            ["make", f"-j{args.jobs}"],
            cwd=REPO,
            check=True,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
    objects = sorted(
        p
        for p in (REPO / "obj").rglob("*.o")
        if p.name not in ("main.o", "simple_run.o")
    )
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-O2",
            "-I" + str(REPO / "include"),
            str(HERE / "driver.cpp"),
            *map(str, objects),
            "-lscip",
            "-lglpk",
            "-o",
            str(out / "driver"),
        ],
        check=True,
    )

    with (out / "trials.csv").open("w") as f:
        f.write("p_idle,p_op,trial,filling_rate\n")
        f.flush()
        subprocess.run([str(out / "driver")], stdout=f, check=True)

    with (out / "trials.csv").open(newline="") as f:
        rows = list(csv.DictReader(f))
    summary = []
    for p_idle in P_IDLE:
        for p_op in P_OP:
            values = [
                float(r["filling_rate"])
                for r in rows
                if float(r["p_idle"]) == p_idle and float(r["p_op"]) == p_op
            ]
            assert len(values) == TRIALS
            summary.append(
                dict(
                    p_idle=p_idle,
                    p_op=p_op,
                    mean=statistics.mean(values),
                    sample_sd=statistics.stdev(values),
                )
            )

    result = dict(
        n=3,
        storage=[36, 90],
        preparation=[12, 45],
        trials=TRIALS,
        steps=100,
        burn_in=20,
        seed_base=998244353,
        algorithm=1,
        source=str(REPO),
        commit=subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=REPO, text=True
        ).strip(),
        source_sha256={
            str(p.relative_to(REPO)): hashlib.sha256(p.read_bytes()).hexdigest()
            for d in ("src", "include")
            for p in (REPO / d).rglob("*")
            if p.is_file()
        },
        results=summary,
    )
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    print(f"Wrote {out / 'trials.csv'} and {out / 'summary.json'}")


if __name__ == "__main__":
    main()
