#!/usr/bin/env python3
"""Measure a fixed editor scene's lit/shadow return after camera orbit.

Consumes shadow_rotation GUI PNGs, not picking/projection results. The session
has no edit gestures, fingerprints authored cells, hides widgets, and pairs
shadows on/off at every pose. Masks are final-image shadow differences; they
are not raw sun-buffer reads or a physical shadow-shape oracle.
"""

import argparse
import json
import math
import re
from pathlib import Path

from PIL import Image, ImageChops

YAWS = (0, .25, 30, 44.75, 45, 45.25, 67, 89.75, 90, 90.25,
        90, 89.75, 67, 45.25, 45, 44.75, 30, .25, 0, 0,
        135, 179.75, 180, 180.25, 225, 269.75, 270, 270.25, 315, 337,
        315, 270.25, 270, 269.75, 225, 180.25, 180, 179.75, 135, 0)
LABEL = r"shadow_rotation_pose(\d+)_(on|off)\b"


def yaw_matches(radians, degrees):
    # Camera pitch recovery reserves 1e-4 radians above -pi at the wrap boundary.
    return abs(math.remainder(radians - math.radians(degrees), math.tau)) < .0002


def difference_mask(first, second, threshold=0):
    bands = ImageChops.difference(first.convert("RGB"), second.convert("RGB")).split()
    strongest = ImageChops.lighter(ImageChops.lighter(bands[0], bands[1]), bands[2])
    return strongest.point(lambda value: 255 if value > threshold else 0)


def count(mask):
    return mask.histogram()[255]


def parse_run(log, directory, surface):
    captures, fingerprints, camera = {}, {}, {}
    pending = None
    expected = {(pose, state) for pose in range(len(YAWS)) for state in ("on", "off")}
    for line in log.splitlines():
        label = re.search(LABEL, line)
        if label:
            key = (int(label[1]), label[2])
            if key not in expected:
                raise ValueError(f"unexpected pose {key}")
            if "GUI-ASSERT " in line:
                if "result=PASS " not in line:
                    raise ValueError(f"failed assertion at {key}")
                if key in fingerprints:
                    raise ValueError(f"duplicate assertion at {key}")
                values = re.search(r"result=PASS actual=active=(\d+) hash=(\d+) "
                                   r"yaw=([-+0-9.eE]+) shadows=([01]) surface=([01])", line)
                if values:
                    active, fingerprint, yaw, shadows, flag = values.groups()
                    if (int(active) != 219 or int(shadows) != (key[1] == "on") or
                            int(flag) != surface or
                            not yaw_matches(float(yaw), YAWS[key[0]])):
                        raise ValueError(f"wrong authored scene or render state at {key}")
                    fingerprints[key] = fingerprint
            if "CaptureCamera:" in line:
                pending = key
                values = re.search(r"yaw=([-+0-9.eE]+) zoom=\(([^)]+)\) "
                                   r"effectiveIso=\(([^)]+)\) subdivisions=(\d+)", line)
                if values:
                    yaw, zoom, pan, subdivisions = values.groups()
                    camera[key] = (float(yaw), zoom, pan, int(subdivisions))
        saved = re.search(r"Saved screenshot: (.+\.png)\s*$", line)
        if saved and pending is not None:
            if pending in captures:
                raise ValueError(f"duplicate capture at {pending}")
            captures[pending] = directory / Path(saved[1]).name
            pending = None
    if captures.keys() != expected or fingerprints.keys() != expected or camera.keys() != expected:
        raise ValueError("missing capture, successful assertion, or camera metadata")
    if len(set(fingerprints.values())) != 1:
        raise ValueError("authored geometry changed during orbit")
    if "CLEAN" not in log or "[gui-verify] 0 layout finding(s)" not in log:
        raise ValueError("run lacks clean shutdown and zero-layout proof")
    for key, pose in camera.items():
        if not yaw_matches(pose[0], YAWS[key[0]]):
            raise ValueError(f"capture yaw differs at {key}")
        if pose[1:] != camera[0, "on"][1:]:
            raise ValueError(f"zoom, pan, or subdivision changed at {key}")
    return captures, next(iter(fingerprints.values()))


def measure(captures, output=None):
    images = {}
    sizes = set()
    for key, path in captures.items():
        with Image.open(path) as image:
            images[key] = image.convert("RGB")
            sizes.add(image.size)
    if len(sizes) != 1:
        raise ValueError("capture dimensions changed")
    width, height = sizes.pop()
    # The diagnostic hides widgets; this central world rectangle also excludes
    # edge overlays and platform window decoration without using engine mapping.
    roi = (width // 5, height * 15 // 100, width * 4 // 5, height * 85 // 100)
    images = {key: image.crop(roi) for key, image in images.items()}
    if output:
        output.mkdir(parents=True, exist_ok=True)
    first_pose, results, failures = {}, [], []
    for pose, yaw in enumerate(YAWS):
        on, off = images[pose, "on"], images[pose, "off"]
        shadow_mask = difference_mask(on, off, 2)
        shadow_pixels = count(shadow_mask)
        item = {"pose": pose, "yaw_degrees": yaw, "shadow_pixels": shadow_pixels,
                "shadow_bounds": shadow_mask.getbbox()}
        if shadow_pixels < 20:
            failures.append(f"pose{pose}: no measurable projected-shadow response")
        if output:
            shadow_mask.save(output / f"pose{pose:02d}-shadow-mask.png")
            ImageChops.difference(on, off).save(output / f"pose{pose:02d}-shadow-difference.png")
            on.save(output / f"pose{pose:02d}-lit.png")
            off.save(output / f"pose{pose:02d}-unshadowed.png")
        if yaw in first_pose:
            reference = first_pose[yaw]
            item["reference_pose"] = reference
            for state in ("on", "off"):
                delta = difference_mask(images[reference, state], images[pose, state])
                changed = count(delta)
                item[f"return_{state}_changed_pixels"] = changed
                if changed:
                    failures.append(f"pose{pose}: {state} failed exact return ({changed} pixels)")
                if output:
                    delta.save(output / f"pose{pose:02d}-return-{state}-mask.png")
        else:
            first_pose[yaw] = pose
        results.append(item)
    return {"pass": not failures, "framebuffer": [width, height], "roi": roi,
            "poses": results, "failures": failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--screenshots", type=Path, required=True)
    parser.add_argument("--surface", type=int, choices=(0, 1), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        captures, fingerprint = parse_run(args.log.read_text(), args.screenshots, args.surface)
        result = measure(captures, args.output)
        result.update(surface=args.surface, authored_hash=fingerprint)
    except (OSError, ValueError, KeyError) as error:
        print(json.dumps({"pass": False, "error": str(error)}))
        return 2
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "metrics.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
