#!/usr/bin/env python3
"""depth-tier-verify — build canvas_stress, run it headless, gate a composite
depth-priority tier at an overlapping-unit pixel.

The deterministic headless tier gate for #2122. It drives a canvas_stress
``--only`` opt-in scene that spawns two world-placed detached units at the same
screen position but different world depth, and asserts the composite winner at
the overlap pixel decodes to the expected priority tier. This is the positive
ENABLED-path guard the priority carriers need — byte-identity at default priority
0 only proves the OFF path is a no-op, never that the carrier is actually
load-bearing. If a future pass drops the carrier the far unit decodes ``tier=0``
(the near unit wins by true depth) and the run FAILs.

Two scenes, selected with ``--only``:
  - ``interpenetrate`` (default, ``--tier 2``, #1960/#2023) — the per-TRIXEL
    carrier. The FAR unit is ROTATED to a fixed non-cardinal pose so its
    re-voxelize fill runs MODE 1 and is tagged the top per-trixel tier; the gate
    proves the carrier survives the rotating fill.
  - ``orbitswap`` (``--tier 1``, #2154) — the per-ENTITY carrier
    (``C_EntityCanvas::depthPriority_``). The FAR unit's whole canvas is pinned
    into the entity-foreground near band (tier 1).
Both scenes are origin-centered and overlap at ``--pixel``, so the same probe
pixel works for either — pass ``--tier`` to match the scene.

Parses the per-frame ``[depth-probe-assert] … tier=N expected=M result=PASS|FAIL``
verdict emitted by ``IRPrefab::DepthProbe::assertCompositeDepthTier``. Exits
non-zero on any FAIL or if no verdict line is found.

Usage:
    python3 scripts/depth-tier-verify.py
    python3 scripts/depth-tier-verify.py --no-build --warmup-frames 8
    python3 scripts/depth-tier-verify.py --pixel 639,362 --tier 2
    python3 scripts/depth-tier-verify.py --only orbitswap --tier 1
"""

import argparse
import re
import subprocess

# Pattern: [depth-probe-assert] pixel=(x,y) normDepth=… rawDist=… enc=…
#          tier=N expected=M result=PASS|FAIL
_ASSERT_RE = re.compile(
    r"\[depth-probe-assert\]\s+"
    r"pixel=\((-?\d+),(-?\d+)\)\s+"
    r"normDepth=\S+\s+rawDist=\S+\s+enc=\S+\s+"
    r"tier=(-?\d+)\s+expected=(-?\d+)\s+result=(PASS|FAIL)"
)

# The interpenetration overlap on canvas_stress's main framebuffer at the
# deterministic --no-spin --no-auto-rotate --zoom 1 pose. The offscreen
# framebuffer is camera-zoom-invariant (zoom magnifies only the window blit), and
# at cam (0,0) the rotation pivots about the world-origin focus, so this pixel
# stays on the far priority unit across the whole auto-screenshot suite.
_DEFAULT_PIXEL = "639,362"
_DEFAULT_TIER = 2
# The canvas_stress --only opt-in scene to drive. Default "interpenetrate" is the
# #1960 per-trixel carrier (tier 2); "orbitswap" is the #2154 per-ENTITY carrier
# (tier 1). Both spawn an origin-centered overlapping pair, so _DEFAULT_PIXEL lands
# on the far priority unit for either — pass --tier to match the scene.
_DEFAULT_ONLY = "interpenetrate"


def _run(cmd: list[str]) -> int:
    """Run a command, streaming output to the terminal; return its exit code."""
    print("+ " + " ".join(cmd), flush=True)
    return subprocess.run(cmd).returncode


def _run_capture(cmd: list[str]) -> tuple[int, str]:
    """Run a command, tee output to the terminal, and return (exit_code, text)."""
    print("+ " + " ".join(cmd), flush=True)
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        text=True, errors="replace",
    )
    lines: list[str] = []
    if proc.stdout is None:
        raise RuntimeError("stdout unavailable (Popen stdout=PIPE failed)")
    for line in proc.stdout:
        print(line, end="", flush=True)
        lines.append(line)
    proc.wait()
    return proc.returncode, "".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Headless composite depth-priority tier gate for canvas_stress."
    )
    parser.add_argument(
        "--only",
        default=_DEFAULT_ONLY,
        metavar="GROUP",
        help=(
            f"canvas_stress --only opt-in group to isolate (default: {_DEFAULT_ONLY}). "
            "Use 'orbitswap' for the #2154 per-entity tier-1 swap gate (--tier 1)."
        ),
    )
    parser.add_argument(
        "--pixel",
        default=_DEFAULT_PIXEL,
        metavar="X,Y",
        help=f"Framebuffer-texture pixel to probe (default: {_DEFAULT_PIXEL})",
    )
    parser.add_argument(
        "--tier",
        type=int,
        default=_DEFAULT_TIER,
        metavar="N",
        help=f"Expected #1960 priority tier at the overlap (default: {_DEFAULT_TIER})",
    )
    parser.add_argument(
        "--warmup-frames",
        type=int,
        default=8,
        metavar="N",
        help="Warmup frames passed as --auto-screenshot N (default: 8)",
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip the fleet-build step (assume the binary is already built)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=120,
        metavar="S",
        help="Watchdog timeout in seconds passed to fleet-run (default: 120)",
    )
    args = parser.parse_args()

    if not args.no_build:
        rc = _run(["fleet-build", "--target", "IRCanvasStress"])
        if rc != 0:
            raise SystemExit(f"[depth-tier-verify] fleet-build failed ({rc})")

    run_cmd = [
        "fleet-run",
        "--timeout", str(args.timeout),
        "IRCanvasStress",
        "--only", args.only,
        "--no-spin", "--no-auto-rotate", "--zoom", "1",
        "--auto-screenshot", str(args.warmup_frames),
        "--depth-probe-assert", f"{args.pixel},tier={args.tier}",
    ]
    run_rc, output = _run_capture(run_cmd)

    verdicts = []
    for m in _ASSERT_RE.finditer(output):
        px, py, tier, expected, result = m.groups()
        verdicts.append(
            dict(pixel=f"({px},{py})", tier=int(tier),
                 expected=int(expected), result=result)
        )

    print()

    if not verdicts:
        print("[depth-tier-verify] no [depth-probe-assert] verdict line found in output")
        raise SystemExit(1)

    failures = [v for v in verdicts if v["result"] == "FAIL"]
    passed = len(verdicts) - len(failures)
    print(
        f"[depth-tier-verify] {passed}/{len(verdicts)} frames PASS at "
        f"{verdicts[-1]['pixel']} (tier={args.tier})"
    )

    if failures:
        bad = failures[0]
        print(
            f"[depth-tier-verify] FAIL: composite winner decoded tier={bad['tier']}, "
            f"expected {bad['expected']} — the per-trixel priority carrier was dropped."
        )

    if failures or run_rc != 0:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
