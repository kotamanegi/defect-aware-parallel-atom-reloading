"""Locate `results/` and `figures/`.

Scripts live under `analysis/`, while data lives at the repository root.
Search parent directories for both `results/` and `Makefile` so paths do not
depend on the current working directory.

Precedence:
  1. Command-line --results-dir / --figures-dir
  2. ATOM_RELOADING_ROOT environment variable
  3. Repository root found by searching upward from the current directory
  4. Repository root found by searching upward from this file
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path

ENV_ROOT = "ATOM_RELOADING_ROOT"


def _looks_like_root(path: Path) -> bool:
    return (path / "results").is_dir() and (path / "Makefile").is_file()


def _search_upwards(start: Path) -> Path | None:
    start = start.resolve()
    for candidate in (start, *start.parents):
        if _looks_like_root(candidate):
            return candidate
    return None


def find_repo_root() -> Path:
    """Return the repository root (the directory containing results/ and Makefile)."""
    env = os.environ.get(ENV_ROOT)
    if env:
        root = Path(env).expanduser().resolve()
        if not _looks_like_root(root):
            raise SystemExit(
                f"{ENV_ROOT}={env} is not a directory containing results/ and Makefile"
            )
        return root

    for start in (Path.cwd(), Path(__file__)):
        root = _search_upwards(start)
        if root is not None:
            return root

    raise SystemExit(
        "Could not find the repository root. "
        f"Run inside the repository, set {ENV_ROOT}, or specify --results-dir / --figures-dir."
    )


def add_path_arguments(parser: argparse.ArgumentParser) -> None:
    """Add --results-dir / --figures-dir arguments."""
    parser.add_argument(
        "--results-dir",
        type=Path,
        default=None,
        help="Directory containing experiment JSON files (default: results/ at the repository root)",
    )
    parser.add_argument(
        "--figures-dir",
        type=Path,
        default=None,
        help="Output directory for figures (default: figures/ at the repository root)",
    )


def resolve_dirs(args: argparse.Namespace) -> tuple[Path, Path]:
    """Resolve (results_dir, figures_dir) from the arguments."""
    results = getattr(args, "results_dir", None)
    figures = getattr(args, "figures_dir", None)
    if results is None or figures is None:
        root = find_repo_root()
        if results is None:
            results = root / "results"
        if figures is None:
            figures = root / "figures"
    return Path(results), Path(figures)
