#!/usr/bin/env python3
"""Guard: every GLSL shader has a Metal counterpart, and every Metal one a GLSL.

The render backends are meant to stay interchangeable
(`docs/design/objectives/backend-parity.md`), so a shader added to one backend
has to arrive on the other or say why not. This audit is presence only: it
pairs files, it never compares their contents.

The tree keeps a prefix on one side of the pairing and drops it on the other:

    c_X.glsl            <-> metal/c_X.metal      compute stage
    ir_X.glsl           <-> metal/ir_X.metal     shared include
    v_X.glsl, f_X.glsl  <-> metal/X.metal        raster pair, bundled in MSL
    g_X.glsl            <-> metal/X.metal        geometry stage, same form

A raster `.metal` file bundles the vertex and fragment stage, so it needs at
least one of `v_X` / `f_X` / `g_X`; a lone `f_X` is legitimate.

A file with no counterpart is either a gap or a deliberate asymmetry.
`parity_allowlist.json` records the deliberate ones, one entry per unpaired
file: `stem` (the file's own stem), `side` (`glsl` or `metal`, the side that
holds the file), `kind`, and `reason`. `kind` is `by-design` when the
counterpart is bundled or co-located, or `deferred` when the port is still
owed; a deferred entry also carries `follow_up`, the issue that owes it. An
entry that no longer matches an unpaired file is itself a failure, so the list
cannot outlive the asymmetry it excuses.

Exit 0: every shader is paired or allowlisted. Exit 1: at least one is not,
the allowlist is malformed, or an entry is stale. Exit 2: the shader tree
cannot be read.
"""

import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
DEFAULT_ROOT = REPO / "engine/render/src/shaders"
DEFAULT_ALLOWLIST = DEFAULT_ROOT / "parity_allowlist.json"

SAME_NAME_PREFIXES = ("c_", "ir_")
RASTER_PREFIXES = ("v_", "f_", "g_")
KINDS = ("by-design", "deferred")
SIDES = ("glsl", "metal")
FOLLOW_UP = re.compile(r"^(?:[\w.-]+/[\w.-]+)?#[0-9]+$")


def glsl_expected_metal(stem):
    """Return the Metal stem a GLSL stem pairs with, or None if unrecognised."""
    if stem.startswith(SAME_NAME_PREFIXES):
        return stem
    for prefix in RASTER_PREFIXES:
        if stem.startswith(prefix) and len(stem) > len(prefix):
            return stem[len(prefix):]
    return None


def metal_expected_glsl(stem):
    """Return the GLSL stems that satisfy a Metal stem; any one is enough."""
    if stem.startswith(SAME_NAME_PREFIXES):
        return [stem]
    return [prefix + stem for prefix in RASTER_PREFIXES]


def load_allowlist(path):
    """Return ({(side, stem): entry}, [problem, ...])."""
    problems = []
    entries = {}
    try:
        raw = json.loads(Path(path).read_text())
    except (OSError, ValueError) as err:
        return entries, [f"allowlist {path}: unreadable ({err})"]
    rows = raw.get("entries") if isinstance(raw, dict) else None
    if not isinstance(rows, list):
        return entries, [f"allowlist {path}: top level must be {{\"entries\": [...]}}"]
    for index, row in enumerate(rows):
        where = f"allowlist entry {index}"
        if not isinstance(row, dict):
            problems.append(f"{where}: not an object")
            continue
        stem, side = row.get("stem"), row.get("side")
        where = f"allowlist entry {index} ({side}:{stem})"
        if not isinstance(stem, str) or not stem:
            problems.append(f"{where}: missing stem")
            continue
        if side not in SIDES:
            problems.append(f"{where}: side must be one of {SIDES}")
            continue
        if row.get("kind") not in KINDS:
            problems.append(f"{where}: kind must be one of {KINDS}")
        reason = row.get("reason")
        if not isinstance(reason, str) or not reason.strip():
            problems.append(f"{where}: reason is empty")
        if row.get("kind") == "deferred":
            follow_up = row.get("follow_up")
            if not isinstance(follow_up, str) or not FOLLOW_UP.match(follow_up):
                problems.append(
                    f"{where}: a deferral needs follow_up, an issue reference like #123"
                )
        if (side, stem) in entries:
            problems.append(f"{where}: duplicate entry")
        entries[(side, stem)] = row
    return entries, problems


def audit(root, allowlist_path):
    """Return the list of failure lines; empty means the tree is clean."""
    root = Path(root)
    metal_dir = root / "metal"
    if not root.is_dir() or not metal_dir.is_dir():
        raise FileNotFoundError(f"no shader tree at {root} (expected metal/ below it)")
    glsl = {p.stem for p in root.glob("*.glsl")}
    metal = {p.stem for p in metal_dir.glob("*.metal")}
    allow, failures = load_allowlist(allowlist_path)
    unpaired = set()

    for stem in sorted(glsl):
        expected = glsl_expected_metal(stem)
        if expected is None:
            failures.append(
                f"unrecognised shader name {stem}.glsl: expected a c_, ir_, v_, f_ or g_ prefix"
            )
        elif expected not in metal:
            unpaired.add(("glsl", stem))
            if ("glsl", stem) not in allow:
                failures.append(
                    f"missing Metal counterpart: {stem}.glsl -> metal/{expected}.metal"
                )
    for stem in sorted(metal):
        candidates = metal_expected_glsl(stem)
        if not any(c in glsl for c in candidates):
            unpaired.add(("metal", stem))
            if ("metal", stem) not in allow:
                names = " or ".join(f"{c}.glsl" for c in candidates)
                failures.append(
                    f"missing GLSL counterpart: metal/{stem}.metal -> {names}"
                )
    for side, stem in sorted(set(allow) - unpaired):
        failures.append(
            f"stale allowlist entry {side}:{stem}: the file is paired or gone, remove the entry"
        )
    return failures, len(glsl), len(metal), len(unpaired & set(allow))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--shader-root", default=str(DEFAULT_ROOT))
    parser.add_argument("--allowlist", default=str(DEFAULT_ALLOWLIST))
    args = parser.parse_args(argv)
    try:
        failures, n_glsl, n_metal, n_allowed = audit(args.shader_root, args.allowlist)
    except FileNotFoundError as err:
        print(f"shader_parity_audit: {err}", file=sys.stderr)
        return 2
    for line in failures:
        print(line)
    if failures:
        print(
            f"shader_parity_audit: {len(failures)} problem(s). Port the missing side, "
            "or add an allowlist entry (kind by-design, or deferred with follow_up) "
            "to engine/render/src/shaders/parity_allowlist.json."
        )
        return 1
    print(
        f"shader_parity_audit: clean, {n_glsl} glsl and {n_metal} metal files, "
        f"{n_allowed} allowlisted"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
