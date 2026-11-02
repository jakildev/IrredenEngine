#!/usr/bin/env python3
"""Stage the attribution arm: the cooperative window with its slab words compiled out.

Rewrites ir_sun_depth_slab_window.{glsl,metal} in every staged shader directory
of a frozen build tree so the window carries the nearest word alone: the slot
init and flush loops, the shared slab claim and the overflow propagation are
removed; the spill path and the nearest-word flush are untouched. The arm's
planes are therefore not exact (no slab is written) and it is never captured,
only timed, to separate the window's own cost from the slab merge's.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts" / "tests"))
from shader_contract_helpers import extract_block  # noqa: E402


def nearest_only(source, suffix):
    claim = extract_block(source, "void writeSunDepthSlabWindow(" if suffix == "glsl"
                          else "inline void writeSunDepthSlabWindow(")
    source = source.replace(claim, claim[:claim.index("{") + 1] + "}")
    loops = re.findall(r"\n[ \t]*for \(uint slot = 0u; slot < kSunDepthSlabSlots; \+\+slot\) \{.*?\n[ \t]*\}",
                       source, re.S)
    if len(loops) != 2:
        raise SystemExit(f"expected the init and flush slot loops, found {len(loops)}")
    for loop in loops:
        source = source.replace(loop, "")
    overflow = extract_block(source, "if (kSunDepthSlabSlots > 0u &&")
    source = source.replace(overflow, "")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tree", type=Path, help="frozen build tree (a copy of the head arm)")
    args = parser.parse_args()
    staged = 0
    for suffix, folder in (("glsl", ""), ("metal", "metal")):
        for path in args.tree.glob(f"**/shaders/{folder}/ir_sun_depth_slab_window.{suffix}"):
            path.write_text(nearest_only(path.read_text(), suffix))
            staged += 1
    if staged == 0:
        raise SystemExit(f"{args.tree}: no staged window shaders")
    print(f"staged nearest-only window in {staged} file(s) under {args.tree}")


if __name__ == "__main__":
    main()
