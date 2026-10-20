#!/usr/bin/env python3
"""Offline exact finite-quad hierarchy experiment; neither a GPU path nor a timing benchmark."""

import argparse
import json
import math
import struct
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path

try:
    from .sun_face_index_probe import read_capture, summarize
except ImportError:
    from sun_face_index_probe import read_capture, summarize


@dataclass(frozen=True)
class Quad:
    corner: tuple
    edge_u: tuple
    edge_v: tuple

    @property
    def bounds(self):
        return tuple((c + min(0, u) + min(0, v), c + max(0, u) + max(0, v))
                     for c, u, v in zip(self.corner, self.edge_u, self.edge_v))

    def blocks(self, point, bias, throw):
        """Exact closed quad footprint and open depth interval, in sun coordinates."""
        u, v = self.edge_u, self.edge_v
        determinant = u[0] * v[1] - u[1] * v[0]
        if determinant == 0:
            return False
        x, y = point[0] - self.corner[0], point[1] - self.corner[1]
        a = (x * v[1] - y * v[0]) / determinant
        b = (u[0] * y - u[1] * x) / determinant
        if not (0 <= a <= 1 and 0 <= b <= 1):
            return False
        depth = self.corner[2] + a * u[2] + b * v[2]
        return bias < point[2] - depth < throw


def load_quads(path):
    """Refuse partial pools or unsupported solids instead of certifying a subset."""
    summary = summarize(path)
    metadata, _ = read_capture(path)
    if metadata.get("geometry_version") != "1" or metadata.get("geometry_record_words") != "10":
        raise ValueError("missing or unsupported geometry format")
    captured = int(metadata.get("geometry_records_captured", "-1"))
    requested, capacity = summary["face_records_requested"], summary["face_record_capacity"]
    if captured != min(requested, capacity):
        raise ValueError("geometry count disagrees with record pool")
    if requested > capacity:
        raise ValueError("record pool exhausted; complete geometry oracle unavailable")
    keys = {key for key in metadata if key.startswith("geometry_record_")
            and key != "geometry_record_words"}
    if keys != {f"geometry_record_{index}" for index in range(captured)}:
        raise ValueError("missing or extra geometry record")
    quads = []
    for index in range(captured):
        words = [int(value) for value in metadata[f"geometry_record_{index}"].split(",")]
        if len(words) != 10 or any(not 0 <= word <= 0xFFFFFFFF for word in words):
            raise ValueError("invalid geometry words")
        if words[9] != 0:
            raise ValueError(f"unsupported analytic record type {words[9]} at {index}")
        values = struct.unpack("<9f", struct.pack("<9I", *words[:9]))
        if not all(math.isfinite(value) for value in values):
            raise ValueError("non-finite geometry")
        values = tuple(Fraction(value) for value in values)
        quads.append(Quad(values[:3], values[3:6], values[6:9]))
    return quads, summary


def overlaps(bounds, point, bias, throw):
    return (bounds[0][0] <= point[0] <= bounds[0][1]
            and bounds[1][0] <= point[1] <= bounds[1][1]
            and bounds[2][1] > point[2] - throw
            and bounds[2][0] < point[2] - bias)


class Hierarchy:
    """Median-split AABB tree retaining every record, including duplicates."""

    def __init__(self, quads, leaf_size=8):
        if leaf_size < 1:
            raise ValueError("leaf size must be positive")
        self.quads = tuple(quads)
        self.bounds = tuple(quad.bounds for quad in quads)
        self.nodes = []
        if quads:
            self._build(tuple(range(len(quads))), leaf_size)

    def _build(self, ids, leaf_size):
        bounds = tuple((min(self.bounds[i][axis][0] for i in ids),
                        max(self.bounds[i][axis][1] for i in ids)) for axis in range(3))
        node = len(self.nodes)
        self.nodes.append(None)
        if len(ids) <= leaf_size:
            self.nodes[node] = (bounds, ids, ())
            return node
        axis = max(range(3), key=lambda a: bounds[a][1] - bounds[a][0])
        ordered = sorted(ids, key=lambda i: sum(self.bounds[i][axis]))
        middle = len(ordered) // 2
        children = (self._build(ordered[:middle], leaf_size),
                    self._build(ordered[middle:], leaf_size))
        self.nodes[node] = (bounds, (), children)
        return node

    def query(self, point, bias, throw):
        stack = [0] if self.nodes else []
        visited = candidates = 0
        while stack:
            bounds, ids, children = self.nodes[stack.pop()]
            visited += 1
            if not overlaps(bounds, point, bias, throw):
                continue
            for index in ids:
                candidates += 1
                if self.quads[index].blocks(point, bias, throw):
                    return True, visited, candidates
            stack.extend(children)
        return False, visited, candidates


def evaluate(quads, points, bias=Fraction(1, 256), throw=Fraction(64), leaf_size=8):
    if not 0 <= bias < throw:
        raise ValueError("require 0 <= bias < shadow throw")
    tree = Hierarchy(quads, leaf_size)
    results, node_visits, candidate_tests, reference_tests = [], [], [], []
    for point in points:
        expected, scanned = False, 0
        for quad in quads:
            scanned += 1
            if quad.blocks(point, bias, throw):
                expected = True
                break
        reference_tests.append(scanned)
        actual, visited, tested = tree.query(point, bias, throw)
        if actual != expected:
            raise ValueError(f"hierarchy disagrees with exhaustive quad oracle at {point}")
        results.append(actual)
        node_visits.append(visited)
        candidate_tests.append(tested)
    if not results:
        raise ValueError("no receiver queries")
    return {
        "oracle": "exact rational finite quads; not shader floating-point equivalence",
        "records": len(quads), "unique_records": len(set(quads)),
        "nodes": len(tree.nodes), "leaf_size": leaf_size,
        "queries": len(results), "blocked": sum(results), "mismatches": 0,
        "node_visits": {"mean": sum(node_visits) / len(results), "max": max(node_visits)},
        "quad_tests": {"mean": sum(candidate_tests) / len(results), "max": max(candidate_tests)},
        "exhaustive_quad_tests": {"mean": sum(reference_tests) / len(results),
                                  "max": max(reference_tests)},
        "record_payload_bytes": 40 * len(quads),
        "hypothetical_packed_tree_bytes": 32 * len(tree.nodes) + 4 * len(quads),
        "gpu_build_clear_query_cost": "unmeasured; CPU prototype only",
        "coverage": results,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--grid", nargs=4, type=Fraction, required=True,
                        metavar=("U_MIN", "V_MIN", "U_MAX", "V_MAX"))
    parser.add_argument("--depth", type=Fraction, required=True, help="sun-space receiver depth")
    parser.add_argument("--steps", type=int, default=17)
    parser.add_argument("--throw", type=Fraction, default=Fraction(64))
    parser.add_argument("--bias", type=Fraction, default=Fraction(1, 256))
    parser.add_argument("--leaf-size", type=int, default=8)
    args = parser.parse_args()
    low_u, low_v, high_u, high_v = args.grid
    if not (2 <= args.steps <= 257 and low_u < high_u and low_v < high_v):
        parser.error("grid must have increasing bounds and 2..257 steps")
    quads, index = load_quads(args.capture)
    points = [(low_u + (high_u - low_u) * x / (args.steps - 1),
               low_v + (high_v - low_v) * y / (args.steps - 1), args.depth)
              for y in range(args.steps) for x in range(args.steps)]
    result = evaluate(quads, points, args.bias, args.throw, args.leaf_size)
    result.update({"capture": str(args.capture), "index": index,
                   "grid": [str(value) for value in args.grid], "steps": args.steps,
                   "depth": str(args.depth), "bias": str(args.bias), "throw": str(args.throw)})
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
