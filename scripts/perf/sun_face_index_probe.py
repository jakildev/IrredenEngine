#!/usr/bin/env python3
"""Summarize capture-only sun face index CSVs; counters do not measure receiver hits."""

import argparse
import csv
import gzip
import json
from pathlib import Path


def summarize(path, world_point=None):
    metadata = {}
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as stream:
        lines = []
        for line in stream:
            if line.startswith("# "):
                key, value = line[2:].strip().split("=", 1)
                metadata[key] = value
            else:
                lines.append(line)
    if metadata.get("active") != "1":
        raise ValueError("finite face index is inactive")
    capacity = int(metadata["tile_capacity"])
    dimension = int(metadata["tiles_per_axis"])
    cascades = int(metadata["cascade_count"])
    if min(capacity, dimension, cascades) <= 0:
        raise ValueError("invalid index dimensions")
    result = [{"occupied_tiles": 0, "incomplete_tiles": 0, "max_counter": 0}
              for _ in range(cascades)]
    projected = None
    if world_point is not None:
        basis = [[float(value) for value in metadata[key].split(",")]
                 for key in ("basis_u", "basis_v")]
        if any(len(axis) != 3 for axis in basis):
            raise ValueError("invalid sun basis")
        projected = [sum(a * b for a, b in zip(axis, world_point)) for axis in basis]
    point_tiles = []
    seen = set()
    for row in csv.DictReader(lines):
        cascade, x, y, count, complete = (int(row[key]) for key in
                                         ("cascade", "x", "y", "count", "complete"))
        key = cascade, x, y
        if (not 0 <= cascade < cascades or not 0 <= x < dimension
                or not 0 <= y < dimension or count < 0 or key in seen):
            raise ValueError("invalid or duplicate tile")
        if complete != int(count <= capacity):
            raise ValueError("tile completeness disagrees with capacity")
        seen.add(key)
        if (projected is not None
                and float(row["u_min"]) <= projected[0] < float(row["u_max"])
                and float(row["v_min"]) <= projected[1] < float(row["v_max"])):
            point_tiles.append({"cascade": cascade, "x": x, "y": y,
                                "counter": count, "complete": bool(complete)})
        stats = result[cascade]
        stats["occupied_tiles"] += count > 0
        stats["incomplete_tiles"] += not complete
        stats["max_counter"] = max(stats["max_counter"], count)
    if len(seen) != cascades * dimension * dimension:
        raise ValueError("missing tile rows")
    summary = {"face_records_requested": int(metadata["face_records_requested"]),
               "face_record_capacity": int(metadata["face_record_capacity"]),
               "tile_capacity": capacity, "cascades": result}
    if projected is not None:
        summary["sun_uv"] = projected
        summary["point_tiles"] = point_tiles
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("captures", nargs="+", type=Path)
    parser.add_argument("--world-point", nargs=3, type=float, metavar=("X", "Y", "Z"))
    args = parser.parse_args()
    results = {str(path): summarize(path, args.world_point) for path in args.captures}
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
