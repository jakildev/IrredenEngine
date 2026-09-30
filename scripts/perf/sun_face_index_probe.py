#!/usr/bin/env python3
"""Summarize capture-only sun face index CSVs; counters do not measure receiver hits."""

import argparse
import csv
import gzip
import json
import math
from pathlib import Path


def summarize(path, world_point=None):
    path = Path(path)
    metadata = {}
    opener = gzip.open if path.suffix == ".gz" else open
    with opener(path, "rt") as stream:
        lines = []
        for line in stream:
            if line.startswith("# "):
                entry = line[2:].strip().split("=", 1)
                if len(entry) != 2 or entry[0] in metadata:
                    raise ValueError("invalid or duplicate capture metadata")
                key, value = entry
                metadata[key] = value
            else:
                lines.append(line)
    if metadata.get("active") != "1":
        raise ValueError("finite face index is inactive")
    required_metadata = {"tile_capacity", "tiles_per_axis", "cascade_count",
                         "face_records_requested", "face_record_capacity"}
    if not required_metadata <= metadata.keys():
        raise ValueError("missing capture metadata")
    capacity = int(metadata["tile_capacity"])
    dimension = int(metadata["tiles_per_axis"])
    cascades = int(metadata["cascade_count"])
    requested = int(metadata["face_records_requested"])
    record_capacity = int(metadata["face_record_capacity"])
    if min(capacity, dimension, cascades, record_capacity) <= 0 or requested < 0:
        raise ValueError("invalid index dimensions")
    result = [{"occupied_tiles": 0, "incomplete_tiles": 0, "max_counter": 0}
              for _ in range(cascades)]
    projected = None
    if world_point is not None:
        if len(world_point) != 3 or not all(math.isfinite(value) for value in world_point):
            raise ValueError("world point must contain three finite coordinates")
        if not {"basis_u", "basis_v"} <= metadata.keys():
            raise ValueError("missing sun basis")
        basis = [[float(value) for value in metadata[key].split(",")]
                 for key in ("basis_u", "basis_v")]
        if any(len(axis) != 3 or not all(math.isfinite(value) for value in axis)
               for axis in basis):
            raise ValueError("invalid sun basis")
        projected = [sum(a * b for a, b in zip(axis, world_point)) for axis in basis]
        if not all(math.isfinite(value) for value in projected):
            raise ValueError("world point projection is not finite")
    point_tiles = []
    seen = set()
    reader = csv.DictReader(lines)
    columns = set(reader.fieldnames or ())
    if len(columns) != len(reader.fieldnames or ()):
        raise ValueError("duplicate tile columns")
    tile_columns = {"cascade", "x", "y", "count", "complete"}
    bounds_columns = {"u_min", "v_min", "u_max", "v_max"}
    if not tile_columns <= columns:
        raise ValueError("missing tile columns")
    has_bounds = bounds_columns <= columns
    if (projected is not None or bounds_columns & columns) and not has_bounds:
        raise ValueError("missing tile bounds")
    for row in reader:
        if None in row or any(value is None for value in row.values()):
            raise ValueError("invalid tile row")
        cascade, x, y, count, complete = (int(row[key]) for key in
                                         ("cascade", "x", "y", "count", "complete"))
        key = cascade, x, y
        if (not 0 <= cascade < cascades or not 0 <= x < dimension
                or not 0 <= y < dimension or count < 0 or key in seen):
            raise ValueError("invalid or duplicate tile")
        if complete != int(count <= capacity):
            raise ValueError("tile completeness disagrees with capacity")
        seen.add(key)
        if has_bounds:
            u_min, v_min, u_max, v_max = (float(row[key]) for key in
                                         ("u_min", "v_min", "u_max", "v_max"))
            if (not all(math.isfinite(value) for value in (u_min, v_min, u_max, v_max))
                    or u_min >= u_max or v_min >= v_max):
                raise ValueError("invalid tile bounds")
            if (projected is not None and u_min <= projected[0] < u_max
                    and v_min <= projected[1] < v_max):
                if any(tile["cascade"] == cascade for tile in point_tiles):
                    raise ValueError("world point lies in overlapping tile bounds")
                point_tiles.append({"cascade": cascade, "x": x, "y": y,
                                    "counter": count, "complete": bool(complete)})
        stats = result[cascade]
        stats["occupied_tiles"] += count > 0
        stats["incomplete_tiles"] += not complete
        stats["max_counter"] = max(stats["max_counter"], count)
    if len(seen) != cascades * dimension * dimension:
        raise ValueError("missing tile rows")
    summary = {"face_records_requested": requested,
               "face_record_capacity": record_capacity,
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
