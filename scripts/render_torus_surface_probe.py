"""Separate torus sample publication from a finite-face representation diagnostic.

The fixture is yaw zero, subdivision eight, major/minor radii four/two,
origin zero, no AO, shadows, or local lights. Accepted samples remain rational
iso-depth positions, not rounded voxel cells. The finite model places unit
cubes at those samples and intersects their union from the camera. This model
is a diagnostic: the publisher's rectangular gather does not promise exact
finite-triangle coverage. A disagreement alone does not prove a renderer bug.
Candidate depths omit the common cardinal storage bias, which cannot change
their ordering. Native same-face owner identity requires a separate owner readback.
"""

import argparse
import hashlib
import json
import math
import subprocess
from collections import Counter, defaultdict
from pathlib import Path

from render_metric_util import read_png

ROOT = Path(__file__).resolve().parents[1]
OFFSETS = (((1, 1), (1, 2)), ((0, 1), (0, 2)), ((0, 0), (1, 0)))
NORMAL_COLORS = ((0, 128, 128), (128, 0, 128), (128, 128, 0))
ISO_X = range(-100, 101)
ISO_Y = range(-132, 133)
DEPTH = range(-115, 116)


def position(i, j, depth):
    return ((2 * depth - 3 * i - j) / 6,
            (2 * depth + 3 * i - j) / 6, (depth + j) / 3)


def samples():
    result = {}
    for j in ISO_Y:
        for i in ISO_X:
            for depth in DEPTH:
                x, y, z = position(i, j, depth)
                if math.hypot(math.hypot(x, y) - 32, z) <= 16.501:
                    result[i, j] = depth
                    break
    return result


def owner_key(i, j, face, half):
    """Row-major 8×8 fixture tiles; native descriptor IDs are not in the PNG."""
    x, y = i + 104, j + 136
    tile, lane = (y // 8) * 26 + x // 8, (y % 8) * 8 + x % 8
    return ((tile * 64 + lane) * 3 + face) * 2 + half


def candidates(accepted):
    result = defaultdict(list)
    for (i, j), depth in accepted.items():
        for face in range(3):
            for half, (dx, dy) in enumerate(OFFSETS[face]):
                result[i + dx, j + dy].append(
                    (8 * depth + face, owner_key(i, j, face, half), i, j, face, half))
    return result


def ray_hits(accepted, u, v):
    """Nearest union boundary, including all samples whose projection covers the ray.

    Unit cubes project within ±1 iso column and ±2 rows of their sample.
    The smallest slab entry over all covering cubes is an exposed union face;
    internal faces cannot precede it. Equal entries retain every normal/owner.
    """
    origin = position(u, v, 0)
    hits = []
    for j in range(math.ceil(v - 2), math.floor(v + 2) + 1):
        for i in range(math.ceil(u - 1), math.floor(u + 1) + 1):
            depth = accepted.get((i, j))
            if depth is None:
                continue
            center = position(i, j, depth)
            entry = [3 * (center[a] - .5 - origin[a]) for a in range(3)]
            leave = min(3 * (center[a] + .5 - origin[a]) for a in range(3))
            near = max(entry)
            if near > leave + 1e-9:
                continue
            axes = [a for a in range(3) if abs(entry[a] - near) < 1e-9]
            hits.append(dict(sample=[i, j, depth], depth=near, axes=axes,
                             point=[origin[a] + near / 3 for a in range(3)]))
    return sorted(hits, key=lambda hit: (hit["depth"], hit["sample"]))


def classify(accepted, candidate, u, v, hits=None):
    hits = ray_hits(accepted, u, v) if hits is None else hits
    _, _, i, j, face, half = candidate
    nearest = [hit for hit in hits if abs(hit["depth"] - hits[0]["depth"]) < 1e-9]
    owner_hits = [hit for hit in hits if hit["sample"][:2] == [i, j]]
    owner_face_covers = any(face in hit["axes"] for hit in owner_hits)
    matching = [hit for hit in nearest if face in hit["axes"]]
    if not hits:
        label = "outside_finite_union"
    elif not owner_face_covers:
        label = "publisher_face_not_on_ray"
    elif not any(hit in nearest for hit in owner_hits):
        label = "publisher_face_occluded"
    elif len({axis for hit in nearest for axis in hit["axes"]}) > 1:
        label = "nearest_edge_tie"
    else:
        label = "nearest_exposed_face"
    return dict(classification=label, candidate_sample=[i, j, accepted[i, j]],
                published_face=face, candidate_half=half,
                normal_matches_nearest=bool(matching) if hits else None,
                nearest=nearest, owner_hits=owner_hits)


def classify_possible_owners(accepted, options, u, v):
    minimum_depth = min(candidate[0] for candidate in options)
    possible = [candidate for candidate in options if candidate[0] == minimum_depth]
    hits = ray_hits(accepted, u, v)
    classifications = [classify(accepted, candidate, u, v, hits) for candidate in possible]
    labels = {item["classification"] for item in classifications}
    return dict(classification=next(iter(labels)) if len(labels) == 1 else "owner_ambiguous",
                normal_matches_nearest=classifications[0]["normal_matches_nearest"],
                possible_owners=classifications)


def verify_origin_contract():
    """Fixed registration: Z1 storage origin is canvas center minus one texel."""
    source = (ROOT / "engine/render/src/shaders/ir_iso_common.glsl").read_text()
    if "return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);" not in source:
        raise ValueError("Z1 origin contract changed; derive registration again")


def compare_image(all_candidates, image):
    width, height, bpp, data = image
    faces = {key: min(options)[4] for key, options in all_candidates.items()}
    expected, observed = Counter(), Counter()
    matches = mismatches = unexpected = 0
    for y in range(height):
        for x in range(width):
            key = ((x - width // 2) // 4 + 1, (y - height // 2) // 2 + 1)
            face = faces.get(key)
            index = (y * width + x) * bpp
            actual = tuple(data[index:index + 3])
            target = (0, 0, 0) if face is None else NORMAL_COLORS[face]
            expected[target] += 1
            observed[actual] += 1
            if face is None:
                unexpected += actual != target
            elif actual == target:
                matches += 1
            else:
                mismatches += 1
    return dict(matching_foreground_pixels=matches, mismatching_foreground_pixels=mismatches,
                unexpected_foreground_pixels=unexpected,
                exact_full_image_color_counts=observed == expected,
                observed_colors={str(color): count for color, count in observed.items()})


def report(accepted, normal_image=None, explain=None):
    verify_origin_contract()
    all_candidates = candidates(accepted)
    counts, examples = Counter(), {}
    faces = Counter()
    image = read_png(str(normal_image)) if normal_image else None
    if image and image[:2] != (2560, 1440):
        raise ValueError("fixture requires the 2560×1440 zoom-eight capture")
    for (i, j), options in all_candidates.items():
        winner = min(options)
        faces[winner[4]] += 1
        for y in range(2):
            for x in range(4):
                u, v = i - 1 + (x + .5) / 4, j - 1 + (y + .5) / 2
                pixel = (1280 + 4 * (i - 1) + x, 720 + 2 * (j - 1) + y)
                classification = classify_possible_owners(accepted, options, u, v)
                label = classification["classification"]
                counts[label] += 1
                normal_match = classification["normal_matches_nearest"]
                normal_category = ("no_finite_hit" if normal_match is None else
                                   "normal_matches_nearest" if normal_match else
                                   "normal_differs_from_nearest")
                counts[normal_category] += 1
                if label not in examples or pixel == explain:
                    examples[label] = dict(pixel=pixel, iso=[u, v], **classification)
                if (not classification["normal_matches_nearest"]
                        and classification["possible_owners"][0]["nearest"]
                        and "normal_differs_from_nearest" not in examples):
                    examples["normal_differs_from_nearest"] = dict(
                        pixel=pixel, iso=[u, v], **classification)
    missing_union_pixels, missing_example = 0, None
    min_i, max_i = min(i for i, _ in accepted), max(i for i, _ in accepted)
    min_j, max_j = min(j for _, j in accepted), max(j for _, j in accepted)
    for y in range(2 * (min_j - 2), 2 * (max_j + 2)):
        for x in range(4 * (min_i - 1), 4 * (max_i + 1)):
            if (x // 4 + 1, y // 2 + 1) in all_candidates:
                continue
            u, v = (x + .5) / 4, (y + .5) / 2
            hits = ray_hits(accepted, u, v)
            if hits:
                missing_union_pixels += 1
                if missing_example is None:
                    missing_example = dict(pixel=[1280 + x, 720 + y], iso=[u, v], nearest=hits[0])
    result = dict(
        fixture="torus yaw=0 subdivision=8 R=4 r=2 origin=0",
        accepted_samples=len(accepted), published_texels=len(all_candidates),
        face_texels=dict(faces), finite_model_pixel_classifications=dict(counts),
        finite_union_unpublished_pixels=missing_union_pixels,
        finite_union_unpublished_example=missing_example,
        examples=examples,
        limits=["Finite unit faces at fractional samples are a representation diagnostic.",
                "Rectangular gather is not exact triangle coverage; discrepancies are not a gate.",
                "All equal encoded-depth owners are classified; native tile IDs are unobserved.",
                "No continuous-torus gradient is used as a face-normal correctness oracle."])
    source_paths = [ROOT / "engine/render/src/shaders" / folder / f"{name}.{backend}"
                    for backend, folder in (("glsl", ""), ("metal", "metal"))
                    for name in ("ir_sdf_common", "ir_iso_common", "c_shapes_to_trixel_body")]
    result["provenance"] = dict(
        source_revision=subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"],
                                       check=True, capture_output=True, text=True).stdout.strip(),
        source_sha256={str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                       for path in source_paths},
        capture_fixture="Assumed yaw=0, origin=0, zoom=8, subdiv=8, radii=(4,2); no AO/shadows",
        registration="2560x1440 center=(1280,720), texel=(4,2), Z1=(-1,-1)")
    if image:
        result["provenance"]["normals_path"] = str(normal_image.resolve())
        result["provenance"]["normals_file_sha256"] = hashlib.sha256(
            normal_image.read_bytes()).hexdigest()
        result["publisher_image_agreement"] = compare_image(all_candidates, image)
    if explain:
        u, v = (explain[0] + .5 - 1280) / 4, (explain[1] + .5 - 720) / 2
        key = (math.floor(u + 1), math.floor(v + 1))
        options = all_candidates.get(key, [])
        result["explain"] = dict(pixel=explain, iso=[u, v], candidates=sorted(options),
                                geometric_hits=ray_hits(accepted, u, v))
        if options:
            result["explain"].update(classify_possible_owners(accepted, options, u, v))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--normals", type=Path)
    parser.add_argument("--pixel", type=int, nargs=2, metavar=("X", "Y"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = report(samples(), args.normals, tuple(args.pixel) if args.pixel else None)
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    agreement = result.get("publisher_image_agreement")
    return int(bool(agreement and (agreement["mismatching_foreground_pixels"]
                                  or agreement["unexpected_foreground_pixels"]
                                  or not agreement["exact_full_image_color_counts"])))


if __name__ == "__main__":
    raise SystemExit(main())
