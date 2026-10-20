"""Shared readers for structured fleet alert records."""

import re
from pathlib import Path

FIELD_RE = re.compile(r"(\w+)=('[^']*'|\S+)")


def parse_fields(text):
    return {
        key: value[1:-1] if value.startswith("'") else value
        for key, value in FIELD_RE.findall(text)
    }


def clone_freshness_records(alerts_dir):
    records = []
    for path in sorted(Path(alerts_dir).glob("clone-freshness-*")):
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
        fields = parse_fields(lines[0] if lines else "")
        fields["name"] = path.name
        records.append(fields)
    return records
