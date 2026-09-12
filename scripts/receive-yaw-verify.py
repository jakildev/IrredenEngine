#!/usr/bin/env python3
"""Gate detached world-receive shadow and Lambert behavior across cardinals."""

from __future__ import annotations

import argparse
import math
import sys
from array import array
from pathlib import Path

import render_metric_util
import verify_common

TARGET = "IRCanvasStress"
DEMO_NAME = "canvas_stress"
SCREENSHOT_SUBDIR = Path("save_files/screenshots")
CARDINAL_COUNT = 4
HALF_EXTENT = 12.0
CENTROID_TOLERANCE = 3.0
COVERAGE_TOLERANCE_PERCENT = 5.0
FLOOR_TOLERANCE = 4
FLOOR_MIN_PIXELS = 400
EXPECTED_LAMBERT = (
    (False, True),
    (False, False),
    (True, False),
    (True, True),
)
EXPECTED_FLOOR = ((108, 45), (45, 45), (45, 87), (87, 108))

Image = tuple[int, int, int, bytes]


def _pixels(image: Image) -> tuple[int, int, int, array[int]]:
    width, height, bytes_per_pixel, pixels = image
    return width, height, bytes_per_pixel, array("B", pixels)


def _receiver_pixels(
    image: Image,
) -> tuple[list[tuple[int, int]], dict[tuple[int, int], tuple[int, int, int]]]:
    width, height, bytes_per_pixel, pixels = _pixels(image)
    points: list[tuple[int, int]] = []
    colors: dict[tuple[int, int], tuple[int, int, int]] = {}
    for y in range(height):
        for x in range(width):
            offset = ((y * width) + x) * bytes_per_pixel
            if pixels[offset + 2] - pixels[offset] > 40:
                point = (x, y)
                points.append(point)
                colors[point] = tuple(pixels[offset + i] for i in range(3))
    return points, colors


def _magenta_pixels(image: Image) -> set[tuple[int, int]]:
    width, height, bytes_per_pixel, pixels = _pixels(image)
    points: set[tuple[int, int]] = set()
    for y in range(height):
        for x in range(width):
            offset = ((y * width) + x) * bytes_per_pixel
            if pixels[offset] > 200 and pixels[offset + 1] < 60 and pixels[offset + 2] > 200:
                points.add((x, y))
    return points


def _rhombus(points: list[tuple[int, int]]) -> tuple[tuple[int, int], ...]:
    top = min(points, key=lambda point: (point[1], point[0]))
    left = min(points, key=lambda point: (point[0], point[1]))
    right = max(points, key=lambda point: (point[0], -point[1]))
    bottom = (left[0] + right[0] - top[0], left[1] + right[1] - top[1])
    return top, left, right, bottom


def _basis(corners: tuple[tuple[int, int], ...]) -> tuple[float, ...]:
    top, left, right, bottom = corners
    center_x = (top[0] + bottom[0]) / 2.0
    center_y = (top[1] + bottom[1]) / 2.0
    return (
        center_x,
        center_y,
        right[0] - center_x,
        right[1] - center_y,
        top[0] - center_x,
        top[1] - center_y,
    )


def _to_top_face(point: tuple[int, int], basis: tuple[float, ...]) -> tuple[float, ...] | None:
    center_x, center_y, right_x, right_y, top_x, top_y = basis
    determinant = right_x * top_y - right_y * top_x
    if abs(determinant) < 1e-6:
        return None
    delta_x = point[0] - center_x
    delta_y = point[1] - center_y
    a = (delta_x * top_y - delta_y * top_x) / determinant
    b = (right_x * delta_y - right_y * delta_x) / determinant
    return HALF_EXTENT * (b - a), HALF_EXTENT * (a + b), a, b


def _face_reading(normal: Image, overlay: Image) -> dict[str, object]:
    receiver, colors = _receiver_pixels(normal)
    if not receiver:
        raise ValueError("capture contains no blue receiver pixels")
    corners = _rhombus(receiver)
    basis = _basis(corners)
    bottom = corners[3]
    top_face: list[tuple[int, int]] = []
    left_face: list[tuple[int, int]] = []
    right_face: list[tuple[int, int]] = []
    for point in receiver:
        mapped = _to_top_face(point, basis)
        if mapped is not None and abs(mapped[2]) + abs(mapped[3]) <= 1.0:
            top_face.append(point)
        elif point[0] < bottom[0]:
            left_face.append(point)
        else:
            right_face.append(point)

    magenta = _magenta_pixels(overlay)
    shadowed = []
    for point in magenta:
        mapped = _to_top_face(point, basis)
        if mapped is not None and abs(mapped[2]) + abs(mapped[3]) <= 1.0:
            shadowed.append((mapped[0], mapped[1]))
    if not shadowed:
        raise ValueError("receiver top face contains no shadow-overlay pixels")

    def mean_red(points: list[tuple[int, int]]) -> float:
        if not points:
            raise ValueError("receiver side-face segmentation is empty")
        return sum(colors[point][0] for point in points) / len(points)

    return {
        "centroid": (
            sum(point[0] for point in shadowed) / len(shadowed),
            sum(point[1] for point in shadowed) / len(shadowed),
        ),
        "coverage": 100.0 * len(shadowed) / max(len(top_face), 1),
        "left_red": mean_red(left_face),
        "right_red": mean_red(right_face),
    }


def _rotate_cardinal(point: tuple[float, float], cardinal: int) -> tuple[float, float]:
    x, y = point
    for _ in range(cardinal):
        x, y = y, -x
    return x, y


def _floor_greys(image: Image) -> dict[int, int]:
    width, height, bytes_per_pixel, pixels = _pixels(image)
    counts: dict[int, int] = {}
    for y in range(height):
        for x in range(width):
            offset = ((y * width) + x) * bytes_per_pixel
            red = pixels[offset]
            blue = pixels[offset + 2]
            if red > 8 and 0 <= blue - red <= 14:
                counts[red] = counts.get(red, 0) + 1
    return counts


def _run_arm(worktree: Path, shots_dir: Path, extra_args: list[str]) -> list[Image]:
    command = [
        "fleet-run",
        TARGET,
        "--only",
        "receiveprobe,floor",
        "--no-spin",
        "--sweep-yaw",
        "0",
        "4.71239",
        str(CARDINAL_COUNT),
        "--auto-screenshot",
        "10",
        *extra_args,
    ]
    return_code, _, shots = verify_common.run_pass(command, cwd=worktree, shots_dir=shots_dir)
    if return_code != 0:
        raise RuntimeError(f"capture arm exited {return_code}, expected ir-run RESULT=CLEAN")
    if len(shots) != CARDINAL_COUNT:
        raise RuntimeError(
            f"capture arm produced {len(shots)} full frames, expected {CARDINAL_COUNT}"
        )
    return [render_metric_util.read_png(str(shot)) for shot in shots]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=None, help="CMake build dir (default: <repo>/build)")
    parser.add_argument("--no-build", action="store_true", help="Skip fleet-build")
    args = parser.parse_args(argv)

    worktree = verify_common.detect_worktree_root(Path.cwd())
    build_dir = Path(args.build_dir) if args.build_dir else worktree / "build"
    backend = verify_common.detect_backend(build_dir)
    print(f"[receive-yaw-verify] target={TARGET} backend={backend}")
    if not args.no_build:
        verify_common.run(["fleet-build", "--target", TARGET], cwd=worktree)

    executable = verify_common.find_exe(build_dir, TARGET, DEMO_NAME)
    shots_dir = executable.parent / SCREENSHOT_SUBDIR
    try:
        normal = _run_arm(worktree, shots_dir, [])
        shadow = _run_arm(worktree, shots_dir, ["--debug-overlay", "shadow"])
        no_shadow = _run_arm(worktree, shots_dir, ["--no-sun-shadows"])
        readings = [_face_reading(normal[i], shadow[i]) for i in range(CARDINAL_COUNT)]
    except (RuntimeError, ValueError) as error:
        print(f"receive-yaw-verify: ERROR {error}")
        return 1

    centroid_passes = 0
    coverage_passes = 0
    baseline_centroid = readings[0]["centroid"]
    baseline_coverage = readings[0]["coverage"]
    for cardinal, reading in enumerate(readings):
        expected = _rotate_cardinal(baseline_centroid, cardinal)
        actual = reading["centroid"]
        distance = math.dist(actual, expected)
        coverage_delta = abs(reading["coverage"] - baseline_coverage)
        centroid_passes += distance <= CENTROID_TOLERANCE
        coverage_passes += coverage_delta <= COVERAGE_TOLERANCE_PERCENT
        print(
            f"  yaw={cardinal * 90:3d} centroid=({actual[0]:+.2f},{actual[1]:+.2f}) "
            f"expected=({expected[0]:+.2f},{expected[1]:+.2f}) distance={distance:.2f} "
            f"coverage={reading['coverage']:.1f}% delta={coverage_delta:.1f}pp"
        )
    centroid_result = "PASS" if centroid_passes == CARDINAL_COUNT else "FAIL"
    coverage_result = "PASS" if coverage_passes == CARDINAL_COUNT else "FAIL"
    print(
        f"receive-yaw-verify: centroid {centroid_result} {centroid_passes}/{CARDINAL_COUNT}, "
        f"coverage {coverage_result} {coverage_passes}/{CARDINAL_COUNT}"
    )

    lambert_passes = 0
    baseline_left = _face_reading(no_shadow[0], shadow[0])["left_red"]
    lambert_details = []
    for cardinal in range(CARDINAL_COUNT):
        reading = _face_reading(no_shadow[cardinal], shadow[cardinal])
        actual = (
            reading["left_red"] >= 1.5 * baseline_left,
            reading["right_red"] >= 1.5 * baseline_left,
        )
        lambert_passes += actual == EXPECTED_LAMBERT[cardinal]
        lambert_details.append(
            f"{cardinal * 90}:{actual} red=({reading['left_red']:.1f},{reading['right_red']:.1f})"
        )
    print("  lambert " + " | ".join(lambert_details))
    lambert_result = "PASS" if lambert_passes == CARDINAL_COUNT else "FAIL"
    print(f"receive-yaw-verify: lambert {lambert_result} {lambert_passes}/{CARDINAL_COUNT}")

    floor_passes = 0
    floor_details = []
    for cardinal, expected_pair in enumerate(EXPECTED_FLOOR):
        counts = _floor_greys(normal[cardinal])
        strong = [value for value, count in counts.items() if count >= FLOOR_MIN_PIXELS]
        passed = all(any(abs(value - expected) <= FLOOR_TOLERANCE for value in strong)
                     for expected in expected_pair)
        floor_passes += passed
        floor_details.append(f"{cardinal * 90}:{sorted(strong, reverse=True)}")
    print("  floor " + " | ".join(floor_details))
    floor_result = "PASS" if floor_passes == CARDINAL_COUNT else "FAIL"
    print(f"receive-yaw-verify: floor {floor_result} {floor_passes}/{CARDINAL_COUNT}")

    return 0 if all(
        passes == CARDINAL_COUNT
        for passes in (centroid_passes, coverage_passes, lambert_passes, floor_passes)
    ) else 1


if __name__ == "__main__":
    sys.exit(main())
