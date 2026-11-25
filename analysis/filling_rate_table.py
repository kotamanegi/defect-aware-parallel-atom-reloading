"""Standalone wrapper for ``atom_reloading_analysis.filling_rate_table``.

Allows running ``python3 analysis/filling_rate_table.py`` with only the Python
standard library, without installing the ``analysis/`` package.
"""

from __future__ import annotations

from pathlib import Path
import sys

_SRC_DIR = str(Path(__file__).resolve().parent / "src")
if _SRC_DIR not in sys.path:
    sys.path.insert(0, _SRC_DIR)

from atom_reloading_analysis.filling_rate_table import (  # noqa: E402,F401
    METHODS,
    ROW_BY_ROW_RESULTS,
    format_cells,
    generate_table,
    main,
)

if __name__ == "__main__":
    main()
