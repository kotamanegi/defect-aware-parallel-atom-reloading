# analysis — Analysis and Plotting Scripts

A Python package that reads atom reloading experiment results from `results/`
at the repository root and generates figures for the paper in `figures/`,
along with statistical summaries.

Dependencies are managed with [uv](https://docs.astral.sh/uv/) and pinned in
`uv.lock`. This package can be used independently of the C++ simulator build.

## Setup

```bash
cd analysis
uv sync
```

If `uv` is not installed, install it with:

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

## Usage

```bash
cd analysis

# filling_rate / runtime / max_planning_time / convergence
uv run plot-metrics

# filling_rate_vs_time (F(t) plot)
uv run plot-filling-rate

# Per-trial statistics and Welch's t-tests
uv run trial-stats
```

You can also run these commands from the repository root:

```bash
make figures   # Run both plot-metrics and plot-filling-rate
make stats     # Run trial-stats
```

### Generating Comparison Tables

Run the following from the repository root to generate a LaTeX `tabular` of
filling rates and planning times from the experiment data. The table is written
to standard output, and the script uses only the Python standard library.

```bash
python3 analysis/filling_rate_table.py --n 3 > /path/to/filling-rate-table.tex
```

Filling rates and standard deviations use four decimal places for every method,
including Baseline, in both noise conditions. Numeric columns are aligned at
the decimal point, so the manuscript must load `\usepackage{dcolumn}`.
Planning times use three decimal places to match the existing tables, or four
for Row-by-row. Incomplete experiments are shown as dashes. Mean planning time
per step is calculated by dividing the mean per-trial planning time by the
number of steps.

Use `--results-dir` to change the input directory. Row-by-row is included when
`results/additional_row_by_row_20260927/results_row_by_row.json` exists. To use
another additional experiment, specify `--row-by-row-results`.
The command does not modify the manuscript.

### Environment Labels and Archived Results

New benchmark JSON uses `Low noise` and `High noise` environment labels.
Plotting, trial statistics, and comparison tables accept both these labels and
the historical Japanese labels, normalizing them to English when loading data.
Files using either label format can be compared together if their experiment
and timing conventions are compatible.

Archived JSON and logs under `results/` are preserved byte for byte, including
their original labels, so recorded checksums and experiment provenance remain
valid. Their numerical data are unchanged by the translation.

## Input and Output Paths

The package locates `results/` and `figures/` by searching upward for a directory
containing both `results/` and `Makefile`. After installation, the commands can
run from any current directory. Use either of the following to set paths explicitly:

```bash
uv run plot-metrics --results-dir /path/to/results --figures-dir /path/to/figures

ATOM_RELOADING_ROOT=/path/to/repo uv run plot-metrics
```

## Structure

```text
analysis/
├── pyproject.toml   # Dependencies and entry points
├── uv.lock          # Pinned versions (commit this file)
└── src/atom_reloading_analysis/
    ├── environments.py         # Normalize current and historical noise labels
    ├── paths.py                # Locate results/ and figures/
    ├── plot_metrics.py         # -> plot-metrics
    ├── plot_filling_rate.py    # -> plot-filling-rate
    └── trial_stats.py          # -> trial-stats
```

After adding or updating dependencies, run `uv lock` and commit the changes to
`uv.lock` together with the dependency changes.
