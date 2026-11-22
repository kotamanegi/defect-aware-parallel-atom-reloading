# Independent Loss-Probability Sweep (2026-11-08)

Archived data for the independent loss-probability sweep of the proposed method
(Fig. 7 of the revised manuscript). The settings and the rerun procedure are
described in
[`experiments/independent_loss_sweep/README.md`](../../experiments/independent_loss_sweep/README.md).

| File | Content | SHA-256 |
| --- | --- | --- |
| `trials.csv` | Raw observations: `p_idle`, `p_op`, trial index, trial-mean filling rate (2,100 rows) | `3f53f7d1ceb9e1146f3f0a7bb7a1d3966cc168080f3b370922a072218cafa93f` |
| `summary.json` | Configuration, simulator commit and source hashes, and the mean and sample SD per condition | `847f15c72e8450df6fffdd8757325900ef1d5655ae7124917696326a49918e26` |

Both files are preserved byte for byte as produced by the original run.

## Provenance

The sweep was run on 2026-11-08 against the development repository, before it
was added here. `summary.json` therefore records development commit
`168d326019114516dde2f1c4ed6ff88332ceba79`, which is not part of this
repository, and the path of the development checkout in `source`.

The `source_sha256` entry records the SHA-256 hashes of all 25 files under `src/`
and `include/` used for the run. All of them are identical to the corresponding
files of this repository's initial public release (`df979aa`), so the sweep runs
on the public source without simulator changes. A rerun on that release
reproduces `trials.csv` byte for byte.
