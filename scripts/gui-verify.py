#!/usr/bin/env python3
"""gui-verify — build a creation, run it headless, parse GUI-ASSERT output, report pass/fail.

Parses the per-assertion result log emitted by IRPrefab::GuiTest::evaluate()
when a creation is run with --auto-screenshot. Exits non-zero on any FAIL.

Usage:
    python3 scripts/gui-verify.py IRVoxelEditor
    python3 scripts/gui-verify.py IRVoxelEditor --warmup-frames 15 --timeout 60
    python3 scripts/gui-verify.py IRVoxelEditor --no-build
    python3 scripts/gui-verify.py IRVoxelEditor -- --gui-session drag_probe
"""

import verify_common


def main() -> None:
    import argparse

    parser = argparse.ArgumentParser(
        description="Run GUI tests headless and report per-assertion pass/fail."
    )
    parser.add_argument("target", help="CMake / ir-run executable name (e.g. IRVoxelEditor)")
    parser.add_argument(
        "--warmup-frames",
        type=int,
        default=10,
        metavar="N",
        help="Warmup frames passed as --auto-screenshot N (default: 10)",
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
    parser.add_argument(
        "target_args",
        nargs="*",
        metavar="-- TARGET_ARG ...",
        help="Extra args forwarded to the target, after a literal -- separator "
             "(e.g. -- --gui-session drag_probe --scene-size 20 20 20)",
    )
    args = parser.parse_args()

    if not args.no_build:
        rc = verify_common.run(["fleet-build", "--target", args.target], check=False)
        if rc != 0:
            raise SystemExit(f"[gui-verify] fleet-build failed ({rc})")

    run_cmd = [
        "fleet-run",
        "--timeout", str(args.timeout),
        args.target,
        "--auto-screenshot", str(args.warmup_frames),
    ] + list(args.target_args)
    run_rc, output = verify_common.run_capture(run_cmd)

    # An --auto-screenshot GUI-test run must self-terminate. The fleet-run
    # watchdog reports a process still alive at --timeout as exit 0 ("healthy"
    # for generic smoke), which would mask a hung GUI test as a zero-assertion
    # success — report_gui_asserts surfaces the ALIVE-TIMEOUT marker as `hung`,
    # which this harness treats as a failure.
    assertions, hung, failures = verify_common.report_gui_asserts(
        output, "[gui-verify] ", timeout=args.timeout)

    if not assertions:
        msg = "[gui-verify] no GUI-ASSERT lines found in output"
        if run_rc != 0 or hung:
            print(msg)
            raise SystemExit(1)
        print(msg + " (run exited 0 — target may have no assertion tables)")
        return

    if failures or run_rc != 0 or hung:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
