"""Plot the independent loss-probability sweep of the proposed method.

Reads results/independent_loss_sweep_20261108/{summary.json,trials.csv} and
writes figures/independent_loss_sweep.pdf.  The plot uses the saved
observations only; it does not run the simulator.  The script first checks that
the summary statistics agree with the raw trials and refuses to plot otherwise.
Error bars are the sample standard deviation of the 100 trial means, not a
confidence interval.
"""

from __future__ import annotations

import argparse
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from .paths import add_path_arguments, resolve_dirs  # noqa: E402
from .results import SWEEP_DIR, load_and_verify_loss_sweep as load_and_verify  # noqa: E402

OUTPUT_NAME = "independent_loss_sweep.pdf"

RCPARAMS = {
    "font.size": 12,
    "axes.labelsize": 12,
    "legend.fontsize": 10,
    "figure.dpi": 150,
    "pdf.fonttype": 42,
}

STYLES = (
    (0.0, "#2ca02c", "o", "-", r"$p_\mathrm{op}=0$ (ideal reloading)"),
    (0.004, "#1f77b4", "s", "--", r"$p_\mathrm{op}=0.004$"),
    (0.020, "#ff7f0e", "^", "-.", r"$p_\mathrm{op}=0.020$"),
)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_path_arguments(parser)
    parser.add_argument(
        "--sweep-dir",
        type=Path,
        default=None,
        help=f"Directory containing summary.json and trials.csv "
        f"(default: <results-dir>/{SWEEP_DIR})",
    )
    args = parser.parse_args()
    results_dir, figures_dir = resolve_dirs(args)
    sweep_dir = args.sweep_dir if args.sweep_dir is not None else results_dir / SWEEP_DIR
    results = load_and_verify(sweep_dir)

    plt.rcParams.update(RCPARAMS)
    fig, ax = plt.subplots(figsize=(6, 4))
    for probability, color, marker, linestyle, label in STYLES:
        points = sorted(
            (result for result in results if result["p_op"] == probability),
            key=lambda result: result["p_idle"],
        )
        ax.errorbar(
            [result["p_idle"] for result in points],
            [100 * result["mean"] for result in points],
            yerr=[100 * result["sample_sd"] for result in points],
            color=color,
            marker=marker,
            linestyle=linestyle,
            linewidth=1.2,
            markersize=4,
            elinewidth=0.8,
            capsize=2.5,
            label=label,
        )

    ax.set_xlabel(r"Storage-loss probability $p_\mathrm{idle}$")
    ax.set_ylabel("Filling rate (%)")
    ax.set_xlim(0, 0.0205)
    # Keep the 100% curves visible just below the top border.
    ax.set_ylim(97, 100.03)
    ax.set_xticks([0, 0.004, 0.008, 0.012, 0.016, 0.020])
    ax.set_xticklabels(["0", "0.004", "0.008", "0.012", "0.016", "0.020"])
    ax.set_yticks([97, 98, 99, 100])
    ax.grid(True, alpha=0.3)
    ax.set_axisbelow(True)
    ax.legend(loc="lower left")
    fig.tight_layout()

    figures_dir.mkdir(parents=True, exist_ok=True)
    output = figures_dir / OUTPUT_NAME
    fig.savefig(output, bbox_inches="tight",
                metadata={"CreationDate": None, "ModDate": None})
    plt.close(fig)
    print(f"Saved {output}")


if __name__ == "__main__":
    main()
