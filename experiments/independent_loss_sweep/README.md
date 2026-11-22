# Independent Loss-Probability Sweep

This experiment varies the storage-loss probability `p_idle` and the reloading
failure probability `p_op` independently for the proposed method. It provides
the data for the figure on independent variation of the loss probabilities
(Fig. 7 of the revised manuscript). The archived data are in
[`results/independent_loss_sweep_20261108/`](../../results/independent_loss_sweep_20261108/),
and the figure is [`figures/independent_loss_sweep.pdf`](../../figures/independent_loss_sweep.pdf).

| File | Content |
| --- | --- |
| `driver.cpp` | Runs the sweep with the existing simulator API and prints one CSV row per trial |
| `run.py` | Builds the simulator, links the driver, runs it, and writes `trials.csv` and `summary.json` |

## Settings

- Proposed method (algorithm ID 1) only.
- Scale factor n = 3: storage 36 × 90, preparation 12 × 45, from
  `makeScaledParameters()`.
- `p_idle` = 0, 0.002, 0.004, 0.008, 0.012, 0.016, 0.020 and
  `p_op` = 0, 0.004, 0.020 (21 conditions).
- 100 trials per condition, indexed 0–99; 100 reloading steps per trial;
  base seed 998244353.
- The storage array starts fully occupied. Each trial averages the post-reload
  filling rate over steps 21–100 (the first 20 steps are discarded as burn-in,
  following the paper's convention). No claim of stationarity is made.
- `summary.json` reports, for each condition, the mean and the sample standard
  deviation across the 100 trial means (not across individual steps).

## Parameter Notes

- The paper's `p_op` is the simulator's `p_reload`.
- `TrialNoise` uses separate random-number streams and thresholds for storage
  loss and reloading failure. The planner receives `p_reload`, not `p_idle`.
- Storage loss removes occupied atoms before planning. Reloading replaces every
  addressed R × C site, including previously occupied sites, with the reloading
  outcome. At `p_reload = 0` every addressed site succeeds.
- The driver asserts that the noise masks contain no failures at zero
  probability and contain failures at nonzero probability.
- The seed derivation includes both probabilities, so each condition is
  reproducible, but conditions are not paired through common random numbers.
- With ideal storage (`p_idle = 0`), the array starts fully occupied and the
  proposed planner does not reload without defects, so `p_op` cannot affect the
  filling rate in this setup.
- The comparison is limited to the proposed method and these sampled values.
  This experiment makes no timing claims and no comparison with other planners.

## Rerunning

Requires `g++`, `make`, SCIP and GLPK (the same requirements as the simulator).
From the repository root:

```bash
python3 experiments/independent_loss_sweep/run.py
cmp experiments/independent_loss_sweep/output/trials.csv \
    results/independent_loss_sweep_20261108/trials.csv
```

The run takes well under a minute on a typical workstation. By default the
script writes the driver binary, `build.log`, `trials.csv` and `summary.json` to
`experiments/independent_loss_sweep/output/` (ignored by Git), so the archived
data are never overwritten. Use `--output-dir` to choose another directory.
The trial outcomes are deterministic: on the initial public release, the rerun
reproduces the archived `trials.csv` byte for byte, and the `results` and
`source_sha256` entries of `summary.json` match the archive. The `source` and
`commit` fields record the checkout used for the rerun.

## Plotting

```bash
make figures                                   # all figures, including this one
uv run --project analysis plot-loss-sweep      # this figure only
```

The plot reads the archived `summary.json` and `trials.csv`, checks that the
summary statistics agree with the raw trials, and writes
`figures/independent_loss_sweep.pdf`. It does not run the simulator. Error bars
are the sample standard deviation of the 100 trial means, not a confidence
interval. To plot a rerun, pass
`--sweep-dir experiments/independent_loss_sweep/output`.
