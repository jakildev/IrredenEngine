#!/usr/bin/env python3
"""Check editor alignment against final PNG pixels and raw cursor samples.

Run IRVoxelEditor --gui-session screen_alignment --gui-layout-check, then pass
its complete GUI log and screenshot directory. The fixture's albedo and normal
frames select the input pixel; this verifier never calls the engine projection,
ray cast, occupancy model, or mouse-to-canvas mapping.
"""

import argparse
import json
import math
import re
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

SOURCE = (224, 64, 32)
PAINT = (60, 200, 120)
NORMALS = {0: (128, 0, 128), 90: (255, 128, 128), 180: (128, 255, 128),
           270: (0, 128, 128), 45: (128, 128, 0),
           30: (128, 128, 0), 67: (128, 128, 0)}
PHASES = ("grid_off", "grid_on", "normals", "hover", "commit", "occluder_base",
          "occluded_ghost", "ghost_only", "floor_hover", "floor_commit")
LABEL = r"screen_alignment_yaw(0|90|180|270|45|30|67)_(" + "|".join(PHASES) + r")\b"


def difference_mask(first, second, tolerance=2):
    bands = ImageChops.difference(first.convert("RGB"), second.convert("RGB")).split()
    maximum = ImageChops.lighter(ImageChops.lighter(bands[0], bands[1]), bands[2])
    return maximum.point(lambda value: 255 if value > tolerance else 0)


def color_mask(image, color):
    return ImageChops.invert(difference_mask(image, Image.new("RGB", image.size, color), 0))


def count(mask):
    return mask.histogram()[255]


def intersection(*masks):
    result = masks[0]
    for mask in masks[1:]:
        result = ImageChops.darker(result, mask)
    return result


def erode(mask, radius=1):
    return mask.filter(ImageFilter.MinFilter(radius * 2 + 1))


def silhouette_support_points(mask):
    bounds = mask.getbbox()
    if bounds is None:
        raise ValueError("source silhouette is absent")
    left, top, right, bottom = bounds
    pixels = mask.load()
    left_bottom = max(y for y in range(top, bottom) if pixels[left, y])
    right_bottom = max(y for y in range(top, bottom) if pixels[right - 1, y])
    lowest = [x for x in range(left, right) if pixels[x, bottom - 1]]
    return ((left, left_bottom), (right - 1, right_bottom),
            (lowest[0], bottom - 1), (lowest[-1], bottom - 1))


def near(mask, point, radius=3):
    x, y = point
    return count(mask.crop((max(x - radius, 0), max(y - radius, 0),
                            min(x + radius + 1, mask.width),
                            min(y + radius + 1, mask.height)))) > 0


def measure_case(images, yaw, sample):
    sizes = {image.size for image in images.values()}
    if len(sizes) != 1:
        raise ValueError("capture dimensions changed within one case")
    size = images["grid_off"].size
    source = color_mask(images["grid_off"], SOURCE)
    source_interior = erode(source)
    source_count = count(source_interior)
    if source_count < 8:
        raise ValueError("source has fewer than eight interior pixels")
    failures = []
    metrics = {"yaw": yaw, "source_interior": source_count}

    def require(condition, name):
        if not condition:
            failures.append(name)

    game_size = sample["game_resolution"]
    if min(game_size) <= 0:
        raise ValueError("game resolution must be positive")
    output_scale = max(1, min(size[i] // game_size[i] for i in range(2)))
    metrics["fit_output_scale"] = output_scale
    cursor = sample["cursor"]
    window = sample["window"]
    framebuffer = sample["framebuffer"]
    require(tuple(framebuffer) == size, "metadata_framebuffer_matches_png")
    if min(window) <= 0:
        raise ValueError("window dimensions must be positive")
    actual_pixel = tuple(math.floor(cursor[i] * size[i] / window[i]) for i in range(2))
    require(actual_pixel == tuple(sample["sample"]), "cursor_roundtrip_matches_sample")
    face = intersection(source, color_mask(images["normals"], NORMALS[yaw]))
    face_interior = erode(face, 2)
    inside_image = 0 <= actual_pixel[0] < size[0] and 0 <= actual_pixel[1] < size[1]
    require(inside_image and face_interior.getpixel(actual_pixel) == 255,
            "raw_cursor_hits_observed_face_interior")
    metrics["cursor_framebuffer_pixel"] = actual_pixel
    metrics["framebuffer_per_window_point"] = [size[i] / window[i] for i in range(2)]

    grid_changes = difference_mask(images["grid_off"], images["grid_on"])
    grid_leaks = count(intersection(grid_changes, source_interior))
    metrics["grid_pixels_over_source"] = grid_leaks
    require(grid_leaks == 0, "solid_occludes_grid")
    support = silhouette_support_points(source)
    support_hits = [near(grid_changes, point) for point in support]
    metrics["support_corner_grid_hits"] = support_hits
    require(all(support_hits), "grid_meets_rendered_bottom_footprint")

    placed = intersection(color_mask(images["commit"], PAINT),
                          ImageChops.invert(color_mask(images["grid_off"], PAINT)))
    placed_interior = erode(placed)
    placed_count = count(placed_interior)
    metrics["placed_interior"] = placed_count
    require(placed_count >= 8, "surface_click_places_visible_voxel")
    ghost_changes = difference_mask(images["hover"], images["grid_off"])
    coverage = count(intersection(ghost_changes, placed_interior)) / max(placed_count, 1)
    translucent = intersection(placed_interior, ghost_changes,
                              difference_mask(images["hover"], images["commit"]))
    translucent_fraction = count(translucent) / max(placed_count, 1)
    metrics["ghost_interior_coverage"] = coverage
    metrics["ghost_translucent_fraction"] = translucent_fraction
    require(coverage >= 0.95, "ghost_covers_whole_committed_cell")
    require(translucent_fraction >= 0.90, "ghost_preserves_background_visibility")
    bounds = placed.getbbox()
    if bounds is not None:
        left, top, right, bottom = bounds
        width, height = right - left, bottom - top
        neighborhood = Image.new("L", size)
        ImageDraw.Draw(neighborhood).rectangle(
            (left - width, top - height, right + width, bottom + height), fill=255)
        red, green, blue = images["hover"].convert("RGB").split()
        outline = intersection(*(band.point(lambda value: 255 if value >= 140 else 0)
                                 for band in (red, green, blue)))
        fill = intersection(ghost_changes, ImageChops.invert(outline))
        # Triangle coverage rounds at the game-pixel boundary. The pale outline
        # adds its one-game-pixel stroke outside that filled footprint.
        fill_permitted = placed.filter(ImageFilter.MaxFilter(output_scale * 2 + 1))
        outline_permitted = placed.filter(ImageFilter.MaxFilter(output_scale * 4 + 1))
        fill_leakage = count(intersection(fill, neighborhood, ImageChops.invert(fill_permitted)))
        outline_leakage = count(intersection(ghost_changes, outline, neighborhood,
                                            ImageChops.invert(outline_permitted)))
        metrics["ghost_fill_band_pixels"] = output_scale
        metrics["ghost_outline_band_pixels"] = output_scale * 2
        metrics["ghost_fill_outside_cell"] = fill_leakage
        metrics["ghost_outline_outside_cell"] = outline_leakage
        metrics["ghost_pixels_outside_cell"] = fill_leakage + outline_leakage
        require(fill_leakage == 0, "ghost_stays_inside_committed_cell_footprint")
        require(outline_leakage == 0, "ghost_outline_stays_inside_stroke_band")

    occluder = erode(color_mask(images["occluder_base"], SOURCE))
    # A ghost-only pixel is recognizably tinted toward the selected green swatch.
    red, green, blue = images["ghost_only"].convert("RGB").split()
    green_over_red = ImageChops.subtract(green, red).point(lambda value: 255 if value > 12 else 0)
    green_over_blue = ImageChops.subtract(green, blue).point(lambda value: 255 if value > 8 else 0)
    ghost_only = intersection(green_over_red, green_over_blue)
    overlap = intersection(occluder, erode(ghost_only))
    overlap_count = count(overlap)
    leaks = count(intersection(overlap, difference_mask(images["occluder_base"],
                                                      images["occluded_ghost"])))
    metrics["ghost_occluded_overlap"] = overlap_count
    metrics["ghost_pixels_through_solid"] = leaks
    require(overlap_count >= 5, "occlusion_control_has_real_overlap")
    require(leaks == 0, "solid_occludes_ghost")

    floor_placed = intersection(color_mask(images["floor_commit"], PAINT),
                                ImageChops.invert(color_mask(images["grid_off"], PAINT)))
    common = count(intersection(source, floor_placed))
    union = count(ImageChops.lighter(source, floor_placed))
    metrics["floor_cell_silhouette_iou"] = common / max(union, 1)
    require(union > 0 and common / union >= 0.95, "floor_click_recreates_source_cell_footprint")
    floor_cursor = sample["floor_cursor"]
    floor_pixel = tuple(math.floor(floor_cursor[i] * size[i] / window[i]) for i in range(2))
    top_face = intersection(source, color_mask(images["normals"], (128, 128, 0)))
    top_bounds = top_face.getbbox()
    source_bounds = source.getbbox()
    if top_bounds is None:
        raise ValueError("source top face is absent")
    # Opposite face centers reflect through the projected cube center; extreme
    # silhouette columns can belong to one edge at a diagonal camera angle.
    support_center = tuple(
        source_bounds[i] + source_bounds[i + 2] - 1
        - (top_bounds[i] + top_bounds[i + 2] - 1) / 2
        for i in range(2))
    require(all(abs(floor_pixel[i] - support_center[i]) <= size[i] / window[i] + 1
                for i in range(2)), "floor_cursor_comes_from_rendered_support_footprint")
    metrics["failures"] = failures
    metrics["pass"] = not failures
    return metrics


def parse_run(log, screenshot_directory):
    captures, samples, poses = {}, {}, {}
    pending = None
    assertion_passes = set()
    screenshot_count = 0
    for line in log.splitlines():
        label = re.search(LABEL, line)
        if label and "GUI-ASSERT " in line:
            if "result=PASS" not in line:
                raise ValueError(f"failed UI assertion: {line}")
            assertion_passes.add((int(label[1]), label[2]))
        if label and "cursor=" in line and "floor_cursor=" in line:
            values = {name: [int(value) for value in pair.split(",")]
                      for name, pair in re.findall(
                          r"\b(cursor|sample|framebuffer|window|floor_cursor)=([0-9]+,[0-9]+)",
                          line)}
            if len(values) == 5:
                samples[int(label[1])] = values
        if label and "CaptureCamera:" in line:
            pending = (int(label[1]), label[2])
            camera = re.search(
                r"yaw=([-+0-9.eE]+) zoom=\(([^)]+)\).*effectiveIso=\(([^)]+)\)"
                r".*gameResolution=\(([^)]+)\)", line)
            if camera:
                poses[pending] = (float(camera[1]), tuple(map(float, camera[2].split(","))),
                                  tuple(map(float, camera[3].split(","))),
                                  tuple(map(int, camera[4].split(","))))
        saved = re.search(r"Saved screenshot: (.+\.png)\s*$", line)
        if saved:
            screenshot_count += 1
            if pending is not None:
                captures[pending] = screenshot_directory / Path(saved[1]).name
                pending = None
    expected = {(yaw, phase) for yaw in NORMALS for phase in PHASES}
    missing = expected - captures.keys()
    if missing:
        raise ValueError(f"missing named captures: {sorted(missing)}")
    if assertion_passes != expected:
        raise ValueError(f"missing successful UI assertions: {sorted(expected - assertion_passes)}")
    clean = re.search(r"^ir-run: RESULT=CLEAN exe=IRVoxelEditor exit=0\s*$", log, re.MULTILINE)
    summary = f"[gui-verify] {len(expected)}/{len(expected)} assertions passed"
    if not clean or summary not in log or "[gui-verify] 0 layout finding(s)" not in log:
        raise ValueError("run has no CLEAN shutdown and zero-layout proof")
    if screenshot_count <= len(expected):
        raise ValueError("F12 did not produce its additional screenshot")
    if set(samples) != set(NORMALS):
        raise ValueError("missing raw cursor/window/framebuffer metadata")
    for (yaw, phase), (actual_yaw, zoom, _, _) in poses.items():
        if abs(math.remainder(actual_yaw - math.radians(yaw), math.tau)) > 0.0001:
            raise ValueError(f"capture yaw differs from label: {yaw}/{phase}: {actual_yaw}")
        if yaw in (30, 67) and any(value.is_integer() for value in zoom):
            raise ValueError(f"expected fractional zoom at {yaw}/{phase}, got {zoom}")
    if set(poses) != expected:
        raise ValueError("missing actual camera metadata")
    if (len({pose[1] for pose in poses.values()}) < 2 or
            len({pose[2] for pose in poses.values()}) < 2):
        raise ValueError("fixture did not exercise distinct zooms and pans")
    for yaw in NORMALS:
        samples[yaw]["game_resolution"] = list(poses[yaw, "hover"][3])
    return captures, samples


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--screenshots", type=Path, required=True)
    args = parser.parse_args()
    try:
        captures, samples = parse_run(args.log.read_text(), args.screenshots)
        results = []
        for yaw in NORMALS:
            images = {}
            for phase in PHASES:
                with Image.open(captures[yaw, phase]) as opened:
                    images[phase] = opened.convert("RGB")
            results.append(measure_case(images, yaw, samples[yaw]))
    except (OSError, ValueError, KeyError) as error:
        print(json.dumps({"pass": False, "error": str(error)}))
        return 2
    passed = all(result["pass"] for result in results)
    print(json.dumps({"pass": passed, "cases": results}, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
