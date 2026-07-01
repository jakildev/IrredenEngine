#!/usr/bin/env python3
"""depth-tier-verify — build canvas_stress, run it headless, gate the #1960
per-trixel-priority tier at an interpenetration pixel.

The deterministic headless tier gate for #2122 (the #2023 / #1960 residual): it
drives ``canvas_stress --only interpenetrate`` (two world-placed detached units
at the same screen position; the FAR one rotated to a fixed non-cardinal pose so
its re-voxelize fill runs MODE 1, and tagged the top per-trixel priority tier)
and asserts the composite winner at the overlap pixel decodes to that tier. This
is the positive ENABLED-path guard the per-trixel carrier needs — byte-identity
at default priority 0 only proves the OFF path is a no-op, never that the carrier
survives the rotating re-voxelize fill. If a future pass drops the carrier the
far unit decodes ``tier=0`` (the near unit wins) and the run FAILs.

Parses the per-frame ``[depth-probe-assert] … tier=N expected=M result=PASS|FAIL``
verdict emitted by ``IRPrefab::DepthProbe::assertCompositeDepthTier``. Exits
non-zero on any FAIL or if no verdict line is found.

Usage:
    python3 scripts/depth-tier-verify.py
    python3 scripts/depth-tier-verify.py --no-build --warmup-frames 8
    python3 scripts/depth-tier-verify.py --pixel 639,362 --tier 2
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
        description="Headless per-trixel-priority tier gate for canvas_stress."
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
        "--only", "interpenetrate",
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
