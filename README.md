# Atom Reloading Simulator

A simulator for atom reloading algorithms in neutral-atom quantum computers.

## Project Structure

```text
defect-aware-parallel-atom-reloading/
├── include/                    # Header files
│   ├── types.h                 # Core type definitions
│   ├── experiment_defaults.h   # Default experiment parameters
│   ├── experiment_metadata.h   # Shared JSON experiment metadata writer
│   ├── experiment_utils.h      # Shared CLI parsing, statistics, and JSON helpers
│   ├── noise.h                 # Per-trial RNG streams and noise masks
│   ├── timing.h                # Wall-clock timing helper
│   ├── grid.h                  # Grid operations
│   ├── planning_utils.h        # Shared input validation and index helpers
│   ├── algorithms.h            # Algorithm interfaces and registry
│   ├── simulator.h             # Simulator interface
│   └── benchmark_runner.h      # Forked cumulative-budget benchmark worker
├── src/                        # Source files
│   ├── types.cpp               # Global variables and grid geometry
│   ├── noise.cpp               # Deterministic noise stream generation
│   ├── algorithm_registry.cpp  # Algorithm registry (one entry per method)
│   ├── grid.cpp                # Grid operations
│   ├── solution/               # Algorithm implementations
│   │   ├── baseline.cpp        # Baseline (Chiu et al.)
│   │   ├── proposed.cpp        # Proposed (greedy initialization + hill climbing)
│   │   ├── greedy.cpp          # Greedy (Algorithm 2 in the paper)
│   │   ├── simple_greedy.cpp   # Simple Greedy (for the ablation study)
│   │   ├── mip_scip.cpp        # MIP (SCIP)
│   │   ├── mip_glpk.cpp        # MIP (GLPK)
│   │   └── row_by_row.cpp      # Cyclic row-by-row comparator
│   ├── simulator.cpp           # Simulator implementation
│   ├── benchmark_runner.cpp    # POSIX child-process budget watchdog
│   ├── main.cpp                # Benchmark program
│   └── simple_run.cpp          # Simple-run program
├── tests/                      # Regression and integration tests
│   ├── algorithms_test.cpp     # Exhaustive small-instance and algorithm checks
│   ├── benchmark_runner_test.cpp # Cumulative-budget watchdog tests
│   ├── benchmark_cli_test.py   # CLI / JSON integration tests
│   └── analysis_test.py        # Python analysis package unit tests
├── results/                    # Experiment results in JSON format
│   ├── results_*.json          # Benchmark results
│   ├── proposed_f_{low,high}.json  # Raw data for F(t) plots
│   ├── additional_row_by_row_20260927/   # Archived row-by-row comparator data
│   └── independent_loss_sweep_20261108/  # Archived loss-probability sweep data
├── experiments/
│   └── independent_loss_sweep/ # Driver and runner for the loss-probability sweep
│       ├── driver.cpp          # Sweep driver linked against simulator objects
│       └── run.py              # Build, run, and summarize the sweep
├── figures/                    # Generated figures
├── analysis/                   # Python analysis and plotting package (uv)
│   ├── pyproject.toml          # Dependencies and entry points
│   ├── uv.lock                 # Pinned dependency versions
│   ├── filling_rate_table.py   # Standalone LaTeX table wrapper (stdlib only)
│   ├── additional_row_by_row_table.py  # Validate and format row-by-row results
│   ├── tests/                  # Analysis compatibility and timeout tests
│   └── src/atom_reloading_analysis/
│       ├── environments.py     # Normalize current and historical noise labels
│       ├── paths.py            # Locate results/ and figures/
│       ├── results.py          # Shared method metadata, loaders, and validators
│       ├── filling_rate_table.py # -> filling-rate-table
│       ├── plot_metrics.py     # -> plot-metrics
│       ├── plot_filling_rate.py # -> plot-filling-rate
│       ├── plot_loss_sweep.py  # -> plot-loss-sweep
│       └── trial_stats.py      # -> trial-stats
├── Makefile                    # Build configuration
└── README.md                   # This file
```

**Input/output conventions:** Store experiment JSON files in `results/` and
generated figures in `figures/`. The benchmark (`./atom_reloading`) writes to
`results/` by default. The analysis and plotting scripts read from `results/`
and write to `figures/`. Output directories are created automatically if needed.

**C++ and Python are independent.** Build the simulator with `make`; Python is
not required. Analysis and plotting use the uv package in `analysis/` and do not
require a C++ build. The two components exchange data only through JSON files
in `results/`.

**Adding an algorithm:** Place the implementation in `src/solution/`, declare it
in `include/algorithms.h`, and add one entry to `src/algorithm_registry.cpp`.
Dispatch, CLI parsing, and display names all use this registry. Existing IDs
must remain unchanged because they are exposed in result JSON and the simple-run
`-a` option. ID 2 is reserved for the removed "Proposed 2" method.

**Grid geometry:** `makeScaledParameters()` in `src/types.cpp` defines the geometry
for scale factor n: a 12n × 30n storage region and a 4n × 15n preparation region.
Both benchmark and simple-run modes use this definition.

## Building

Both SCIP and GLPK MIP implementations are included in the same binaries, so
building requires both `libscip` and `libglpk`. Select the solver at runtime with
`-a mip_scip` or `-a mip_glpk` in benchmark mode.

```bash
# Build all programs
make

# Run algorithm, benchmark-runner, and CLI regression tests
make test

# Build and run the benchmark program
make run

# Build and run the simple-run program
make run-simple

# Build with debug information
make debug

# Remove build artifacts
make clean

# Show help
make help
```

## Usage

### Benchmark Mode

Evaluate each algorithm across multiple parameter sets and write the results as JSON.

```bash
./atom_reloading [options]
```

**Options:**

- `-a <name>`: Algorithm: `baseline` / `proposed` / `mip_scip` / `greedy` / `simple_greedy` / `mip_glpk` / `row_by_row` / `all` (default: `all`).
- `-o <path>`: Output file (default: `results/results_<algo>.json`; ignored when running multiple algorithms).
- `-n <value>`: Maximum scale factor n (default: 15; minimum: 1).
- `-c <value>`: Average budget per planning call in milliseconds. The cumulative budget is `c × num_trials × num_iterations` ms; execution stops when the accumulated planning time exceeds it (default: 100).
- `-H`: Run only the high-noise case (default: both low- and high-noise cases).
- `-h, --help`: Show help.

The rectangular grid dimensions are determined by n: 12n × 30n for the storage
region and 4n × 15n for the preparation region.

The default `-c 100` gives a cumulative budget of 100 × 10,000 = 1,000,000 ms for
100 trials × 100 operations = 10,000 planning calls. This is a cumulative limit,
not a fixed limit on each call. The measured duration of every planning call is
added to the total; faster operations leave more of the budget for later calls.

Each n runs in a child process. Immediately before each planning call, an OS
timer is set to the remaining budget and is disarmed immediately afterward.
If a single call exceeds the remaining budget, the child is terminated without
waiting for the solver to return. This watchdog enforces the cumulative budget;
it does not impose a fixed per-call limit such as 100 ms.
Larger n values are then skipped for that algorithm and noise condition, while
other algorithms and noise conditions continue. The budget covers every planning
call, including burn-in, and excludes initialization, noise generation, reloading,
and aggregation. The implementation uses POSIX `fork`, `setitimer`, and `SIGALRM`.
Timer rounding to microseconds and OS scheduling may delay termination slightly.

JSON is saved whenever an n completes or times out. Each row has a `status` of
`completed` or `timeout`. `num_trials_done` counts completed trials;
`num_operations_done` counts operations that completed reloading, including those
in an unfinished trial. `elapsed_ms` is the elapsed time for the entire n run.
Timeout rows include the one-based `timeout_trial` and `timeout_operation`, plus
`timeout_planning_wall_ms`: the cumulative planning time at cutoff, or the elapsed
time of the final planning call if the child was terminated during that call.

Trial statistics include only completed trials. If none completed, statistics
are stored as `null` and trial arrays are empty. Planning-time statistics include
only completed operations and exclude interrupted calls. Plotting and trial
statistics commands exclude timeout rows.

Both mean and maximum planning times measure only `spec->plan(...)` calls. They
exclude initialization, noise generation, reloading, filling-rate aggregation,
timer setup, and notifications. `trial_runtimes_ms` contains the sum of planning
times for all operations in each completed trial, including burn-in. `runtime_ms`
and `runtime_ms_std` are the mean and sample standard deviation of these sums
across trials. `runtime_ms / num_iterations` is the mean planning time per
operation (Time/step). `max_planning_wall_ms` is the maximum duration of a single
planning call among completed operations. `elapsed_ms` includes simulator work
and is intended for tracking experiment progress.

This timing scope is identified by `schema_version: 4` and `runtime_time_scope`.
In schemas 2 and 3, `runtime_ms` includes simulator work. Rerun experiments to
obtain measurements with the new scope. Plotting rejects result files with
incompatible timing scopes.

```bash
# Evaluate all algorithms for n=1..15 (the range used in the paper; also the default)
./atom_reloading -n 15

# Evaluate only the proposed method in the high-noise case (writing to a custom path)
./atom_reloading -a proposed -H -o /tmp/results_proposed.json

# Quick check across all algorithms up to n=1 without overwriting archived results/
repo="$PWD" && mkdir -p /tmp/repro && (cd /tmp/repro && "$repo"/atom_reloading -n 1)
```

Results are saved to `results/results_<algo>.json` relative to the current working
directory by default.

**Preserving archived results & runtime expectations:** Running `./atom_reloading`
or `make run` from the repository root overwrites the archived `results/results_*.json`
files. To keep the archived data untouched, pass `-o <path>` for a single algorithm
or run the binary from a separate working directory (as in the quick-check example
above). Note also that while the heuristic methods (`baseline`, `greedy`,
`simple_greedy`, `proposed`, `row_by_row`) finish quickly, exact MIP runs
(`mip_scip`, `mip_glpk`) can run up to the full cumulative budget (1,000 s per
noise condition at the default `-c 100`) before timing out at larger `n`. Start with
a small `-n` (such as `-n 1` or `-n 3`) or a single method when doing a quick
verification.

### Simple-Run Mode

Run multiple trials with the specified parameters and report the filling rate
F(t) at each iteration, with its mean and sample standard deviation across trials.
The grid geometry is shared with benchmark mode through `makeScaledParameters()`.

```bash
./atom_reloading_simple [options]
```

**Options:**

- `-n <value>`: Scale factor n: storage 12n × 30n, preparation 4n × 15n (default: 15).
- `-a <value>`: Algorithm ID (default: 1). See the table below.
- `-p_idle <value>`: Environmental noise probability (default: 0.004).
- `-p_reload <value>`: Transport failure probability during reloading (default: 0.004).
- `-i <value>`: Number of iterations (default: 100).
- `-t, --trials <value>`: Number of trials (default: 100).
- `-s, --seed <value>`: Base random seed (default: 998244353).
- `-o, --output <path>`: Output JSON file (default: `execution_result.json`).
- `-h, --help`: Show help.

| ID | Algorithm |
| --- | --- |
| 0 | Baseline (Chiu et al.) |
| 1 | Proposed (greedy initialization + hill climbing) |
| 3 | MIP / SCIP (exact solution) |
| 4 | Greedy (Algorithm 2 in the paper) |
| 5 | Simple Greedy (for the ablation study) |
| 6 | MIP / GLPK (exact solution) |
| 7 | Row-by-row (cyclic, reloading defects only) |

**Examples:**

```bash
# Run the proposed method with default settings
./atom_reloading_simple

# Run Greedy with n=50
./atom_reloading_simple -n 50 -a 4

# Run Baseline in the high-noise case
./atom_reloading_simple -n 30 -a 0 -p_idle 0.02 -p_reload 0.02

# Regenerate raw data for the F(t) plots (results/proposed_f_*.json, n=15)
# Use a different -o path if you want to keep the archived files untouched.
./atom_reloading_simple -n 15 -a 1 -i 100 -t 100 -o results/proposed_f_low.json
./atom_reloading_simple -n 15 -a 1 -i 100 -t 100 -p_idle 0.02 -p_reload 0.02 \
    -o results/proposed_f_high.json
```

Results are saved to `execution_result.json` by default; use `-o` to change it.

### Additional Row-by-Row Experiment

The cyclic row-by-row comparator (`row_by_row`) is archived separately under
`results/additional_row_by_row_20260927/` and is used for comparison tables rather
than the main plots.

```bash
# Rerun the row-by-row benchmark without overwriting the archive
./atom_reloading -a row_by_row -n 15 -o /tmp/results_row_by_row.json

# Validate the JSON and generate the summary CSV and LaTeX table
python3 analysis/additional_row_by_row_table.py \
    results/additional_row_by_row_20260927/results_row_by_row.json \
    --output-dir /tmp/row_by_row_out
```

### Independent Loss-Probability Sweep

The paper's sweep that varies `p_idle` and `p_op` independently for the proposed
method (Fig. 7 of the revised manuscript) uses a small driver linked against
the simulator objects, without simulator changes. The archived data are in
`results/independent_loss_sweep_20261108/`.

```bash
# Rerun the sweep; writes to experiments/independent_loss_sweep/output/
python3 experiments/independent_loss_sweep/run.py

# Compare the rerun with the archived observations
cmp experiments/independent_loss_sweep/output/trials.csv \
    results/independent_loss_sweep_20261108/trials.csv
```

See [experiments/independent_loss_sweep/README.md](experiments/independent_loss_sweep/README.md)
for the settings, parameter notes, and the expected reproducibility.

## Plotting and Analysis

The paper's figures in `figures/` can be regenerated from the JSON files in
`results/`. Analysis and plotting use the [uv](https://docs.astral.sh/uv/) package
in `analysis/`, with dependencies pinned in `analysis/uv.lock`.

```bash
# Regenerate all figures
make figures

# Show per-trial statistics and Welch's t-tests
make stats
```

To run commands individually:

```bash
cd analysis
uv sync                    # First run: install dependencies from uv.lock

uv run plot-metrics        # filling_rate / runtime / max_planning_time / convergence
uv run plot-filling-rate   # filling_rate_vs_time (F(t) plot)
uv run plot-loss-sweep     # independent_loss_sweep (independent loss-probability sweep)
uv run trial-stats         # Per-trial statistics and Welch's t-tests
uv run filling-rate-table --n 3  # Decimal-aligned LaTeX comparison table (stdout)
```

### Artifact-to-Command Reproduction Map

| Output artifact | Input data | Command |
| --- | --- | --- |
| `figures/filling_rate_{low,high}_noise.pdf`, `figures/runtime_{low,high}_noise.pdf`, `figures/max_planning_time_{low,high}_noise.pdf`, `figures/convergence_iterations.pdf` | `results/results_*.json` | `make figures` or `uv run --project analysis plot-metrics` |
| `figures/filling_rate_vs_time.pdf` | `results/proposed_f_{low,high}.json` | `make figures` or `uv run --project analysis plot-filling-rate` |
| `figures/independent_loss_sweep.pdf` (Fig. 7) | `results/independent_loss_sweep_20261108/{summary.json,trials.csv}` | `make figures` or `uv run --project analysis plot-loss-sweep` |
| Per-trial summary and Welch's t-tests (stdout) | `results/results_*.json` | `make stats` or `uv run --project analysis trial-stats` |
| Main comparison LaTeX table (stdout) | `results/results_*.json` (+ `results/additional_row_by_row_20260927/results_row_by_row.json` if present) | `python3 analysis/filling_rate_table.py --n 3` (stdlib only) or `uv run --project analysis filling-rate-table --n 3` |
| Additional row-by-row CSV, LaTeX table, and `validation.json` | `results/additional_row_by_row_20260927/results_row_by_row.json` | `python3 analysis/additional_row_by_row_table.py <result.json> --output-dir <dir>` (stdlib only) |

The locations of `results/` and `figures/` are resolved automatically, so commands
work from any current directory once the package is installed. To override the
paths, use `--results-dir` / `--figures-dir` or the `ATOM_RELOADING_ROOT` environment
variable.

See [analysis/README.md](analysis/README.md) for details.

## Algorithms

### Baseline (Chiu et al.)

- Reloads atoms using fixed row-modulo-3 and column-modulo-2 patterns.
- Selects the pattern deterministically from the time step.

### Proposed

- Uses Greedy (Algorithm 2 / `greedyMethod`) to construct one initial solution,
  then optimizes (R, C) by hill climbing.
- Reuses the u/v scores from Greedy as initial hill-climbing scores, matching
  the incremental updates in the paper.
- Adapts to the current vacancy locations.

### Greedy

- Selects rows and columns greedily so that |R|/|C| approaches the preparation
  region's aspect ratio.
- Evaluates the proposed method's initialization algorithm as a standalone baseline.

### Simple Greedy

- Independently selects rows and columns with the largest numbers of vacancies;
  used as a simplified comparator in the ablation study.

### MIP (SCIP) / MIP (GLPK)

- Solves the mixed-integer program exactly. The solvers have no internal time
  limit; the benchmark stops the run when the cumulative `-c` budget is exceeded.
- Provides two implementations of the same formulation, both built together
  and selected separately with `-a mip_scip` / `-a mip_glpk`.
- Both solvers achieve the same optimal objective value. When multiple optimal
  solutions exist, solver-dependent choices can lead to different simulation
  trajectories and slightly different filling rates.

### Row-by-row

- Cycles through rows and selects up to `m_c` vacant sites in the current row,
  in ascending column order. A full row still consumes one time step.
- Serves as a separate comparator for the additional experiment; its results
  can be included in comparison tables.

## Parameters and Conventions

- **Low-noise case:** p_idle=0.004, p_reload=0.004.
- **High-noise case:** p_idle=0.02, p_reload=0.02.
- **Trials:** 100 in benchmark mode.
- **Reloading operations:** 100 by default. Both modes exclude the first 20 as
  burn-in and average over operations 21–100 (80 operations).
- **Time and positions:** JSON time indices start at 1 after the first operation.
  Baseline converts the paper's one-based positions to zero-based array indices.
- **Simple-run operation count:** `-i` must exceed 20. For T operations, the mean
  covers operations 21–T (T−20 operations). Time series include all operations,
  including burn-in.
- **Output compatibility:** Schema version 4 records burn-in, time and Baseline
  indexing conventions, and the planning-only timing scope. Avoid combining
  results with incompatible conventions. New benchmark output uses `Low noise`
  and `High noise` environment labels. Analysis commands also accept historical
  Japanese labels and normalize them to English. Archived JSON and logs in
  `results/` retain their original contents to preserve experiment provenance
  and recorded checksums.
- **Commit ID:** Both modes record the build-time HEAD commit as `commit_id` in
  JSON metadata. The Makefile generates `obj/git_commit.h` automatically. Without
  Git, the value is `unknown`; uncommitted changes to tracked files add `-dirty`.

## Output

### Benchmark Mode

Each parameter set and algorithm produces JSON containing:

- Atom filling rate (mean and standard deviation).
- Planning time (per trial, per operation, and maximum single-call duration).
- Hill-climbing iterations until convergence (Proposed only).

### Simple-Run Mode

Outputs the parameters, algorithm, experiment metadata, overall mean filling rate,
and per-iteration filling-rate means and sample standard deviations. Grid states
and individual reloading instructions are not stored.

## Experiment Provenance

This public repository starts with a source snapshot from development commit
`3bf7bf6d70baf364f7fd99507b900d03c8995587`, including the English documentation
and software messages.

The archived experiments were run before this public repository was initialized.
Their `commit_id` and `simulation_commit` fields refer to the original development
history, whose commits are not part of this repository. Source hashes and result
checksums also refer to the recorded experimental artifacts. These identifiers,
the original result files, and the measured values are preserved unchanged.

New experiments record the commit ID of the public source used to build the
simulator. Use the archived metadata to identify each experiment's conditions;
planning times from reruns may differ with the execution environment.

The independent loss-probability sweep in `results/independent_loss_sweep_20261108/`
was run in the development repository on 2026-11-08, after this repository was
initialized, and records development commit
`168d326019114516dde2f1c4ed6ff88332ceba79`. The SHA-256 hashes it records for all
files under `src/` and `include/` match this repository's initial public release
(`v1.0.0`), and a rerun on that release reproduces both `trials.csv` and
`source_sha256`. Subsequent code-cleanup commits change `source_sha256` and
`commit`, while leaving the algorithms, noise model, and seed derivation unchanged,
so reruns continue to reproduce the archived `trials.csv` byte for byte (as well
as the deterministic filling-rate results in the benchmark and simple-run archives).
