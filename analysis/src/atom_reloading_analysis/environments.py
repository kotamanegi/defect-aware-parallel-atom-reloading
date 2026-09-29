"""Normalize environment labels without modifying archived experiment files."""

# Historical JSON uses Japanese labels; new benchmark output uses English.
_ENVIRONMENT_LABELS = {
    "低ノイズ": "Low noise",
    "高ノイズ": "High noise",
}


def normalize_environment(label: str) -> str:
    """Return the English label, leaving unknown labels unchanged."""
    return _ENVIRONMENT_LABELS.get(label, label)
