"""Recompute the sun depth-slab collision census from a primary-rect probe CSV.

Stdlib-only, so the census and its tests run on a host without Pillow; the
capture driver (`sun_depth_slab_capture.py`) writes the CSVs and calls this.
"""

import collections
import csv

EMPTY = 0xFFFFFFFF


def depth_of(word):
    return (word >> 8) / 1024.0 - 512.0


def summarize_probe(path):
    """Per-cascade slab occupancy under the receiver: distinct depth bands per texel,
    overflow count, and the depth histogram of nearest words and slab extrema."""
    header, per_cascade = {}, {}
    with open(path) as stream:
        for row in csv.reader(stream):
            if not row:
                continue
            if row[0].startswith("#"):
                key, _, value = row[0][2:].partition("=")
                header[key] = ",".join([value] + row[1:])
                continue
            if row[0] == "cascade":
                columns = {name: index for index, name in enumerate(row)}
                slots = sum(name.endswith("_min") for name in row)
                continue
            cascade = int(row[0])
            entry = per_cascade.setdefault(cascade, {
                "texels": 0, "overflow": 0, "direct_collisions": 0,
                "complete_rows": 0, "complete_modulo_collisions": 0,
                "overflowed_rows": 0, "overflowed_retained_modulo_collisions": 0,
                "slab_count_histogram": collections.Counter(),
                "nearest_depths": collections.Counter(), "slab_extrema": collections.Counter()})
            entry["texels"] += 1
            overflow = int(row[columns["overflow"]])
            entry["overflow"] += overflow
            direct = header.get("slab_mode") == "direct"
            collision = False
            slab_ids = []
            nearest = int(row[3])
            if nearest != EMPTY:
                entry["nearest_depths"][round(depth_of(nearest))] += 1
            slabs = 0
            for slot in range(slots):
                low, high = int(row[4 + 2 * slot]), int(row[5 + 2 * slot])
                if low == EMPTY:
                    continue
                slabs += 1
                low_slab, high_slab = (low >> 8) // 65536, (high >> 8) // 65536
                slab_ids.append(low_slab)
                collision |= low_slab != high_slab
                band = f"{(low >> 8) // 65536}:{round(depth_of(low))}..{round(depth_of(high))}"
                entry["slab_extrema"][band] += 1
            entry["slab_count_histogram"][slabs] += 1
            if direct:
                if int(row[columns["collision"]]) != int(collision):
                    raise ValueError("direct collision column disagrees with raw extrema")
                entry["direct_collisions"] += collision
            elif slots:
                modulo_collision = len({slab % slots for slab in slab_ids}) < len(set(slab_ids))
                if overflow:
                    entry["overflowed_rows"] += 1
                    entry["overflowed_retained_modulo_collisions"] += modulo_collision
                else:
                    entry["complete_rows"] += 1
                    entry["complete_modulo_collisions"] += modulo_collision
    summary = {"header": header, "cascades": {}}
    for cascade, entry in sorted(per_cascade.items()):
        summary["cascades"][str(cascade)] = {
            **{key: entry[key] for key in (
                "texels", "overflow", "direct_collisions", "complete_rows",
                "complete_modulo_collisions", "overflowed_rows",
                "overflowed_retained_modulo_collisions")},
            "slab_count_histogram": dict(sorted(entry["slab_count_histogram"].items())),
            "nearest_depths": dict(sorted(entry["nearest_depths"].items())),
            "slab_extrema": dict(sorted(entry["slab_extrema"].items())),
        }
    return summary
