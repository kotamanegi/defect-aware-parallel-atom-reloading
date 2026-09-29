"""Plot filling rate F(t) over time for proposed method.

Reads results/proposed_f_{low,high}.json and writes to
figures/filling_rate_vs_time.pdf.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

from .paths import add_path_arguments, resolve_dirs  # noqa: E402

RCPARAMS = {
    "font.size": 12,
    "axes.labelsize": 12,
    "legend.fontsize": 10,
    "figure.dpi": 150,
}

FILES = {
    "Low noise ($p_\\mathrm{idle}=p_\\mathrm{op}=0.004$)": "proposed_f_low.json",
    "High noise ($p_\\mathrm{idle}=p_\\mathrm{op}=0.020$)": "proposed_f_high.json",
}

COLORS = {
    "Low noise ($p_\\mathrm{idle}=p_\\mathrm{op}=0.004$)": "#2ca02c",
    "High noise ($p_\\mathrm{idle}=p_\\mathrm{op}=0.020$)": "#ff7f0e",
}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_path_arguments(parser)
    args = parser.parse_args()
    results_dir, figures_dir = resolve_dirs(args)

    plt.rcParams.update(RCPARAMS)

    fig, ax = plt.subplots(figsize=(6, 4))
    ax.grid(True, alpha=0.3)

    for label, fname in FILES.items():
        with open(results_dir / fname) as f:
            data = json.load(f)
        # Convert zero-based indices in legacy JSON to one-based post-update times in plots.
        offset = 1 - data.get("time_index_base", 0)
        t = [i["iteration"] + offset for i in data["iterations"]]
        fr = [i["filling_rate"] * 100 for i in data["iterations"]]
        sd = [i.get("filling_rate_std", 0.0) * 100 for i in data["iterations"]]
        if any(s > 0 for s in sd):
            ax.errorbar(t, fr, yerr=sd, linewidth=1.0, elinewidth=0.6,
                        capsize=1.5, markersize=0, color=COLORS[label],
                        label=label, alpha=0.9)
        else:
            ax.plot(t, fr, linewidth=1.0, color=COLORS[label], label=label)

    ax.set_xlabel("Iteration $t$")
    ax.set_ylabel(r"Filling rate $\overline{F}(t)$ (%)")
    ax.set_ylim(top=100)
    # The right-hand gap between the noise curves leaves room for both labels.
    ax.legend(loc="center right")

    figures_dir.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    path = Path(figures_dir) / "filling_rate_vs_time.pdf"
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    print(f"Saved: {path}")


if __name__ == "__main__":
    main()
