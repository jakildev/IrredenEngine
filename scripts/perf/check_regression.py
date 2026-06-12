#!/usr/bin/env python3
"""check_regression.py — fingerprint-aware regression gate over compare_perf_runs.

Loads the head run, resolves the matching fingerprinted baseline under the
provided baseline-root, prints the full compare_perf_runs markdown table
(with host-calibration block), then exits according to the gate decision
tree from #1074:

    Same fingerprint, lock uncontested → check raw deltas, fail on regression.
    Same fingerprint, lock contended   → check normalized deltas instead.
    Different fingerprint              → informational only, pass.
    No baseline at all                 → informational only, pass (seed-new).

Usage:
    scripts/perf/check_regression.py <baseline_root> <head_dir>
        [--regress-pct N] [--improve-pct N] [--gpu-only] [--cpu-only]

Exit codes:
    0   No regression above threshold (pass — may still show improvements)
    1   One or more cells regressed by more than --regress-pct (fail)
    2   Usage error
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPTS_PERF = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS_PERF))
from compare_perf_runs import (  # noqa: E402
    LOAD_FACTOR_TRUST_NORMALIZED,
    build_host_note,
    host_slug,
    load_factor,
    load_manifest,
    load_run,
    normalize_ms,
    pct_delta,
    render_markdown,
    resolve_baseline,
)


def _regressed_cells(base, head, regress_pct: float, *,
                     ref_ms: float, target_ms: float, use_normalized: bool) -> list[str]:
    out = []
    for cell_id in sorted(set(base) & set(head)):
        avg_b = base[cell_id].frame.avg
        avg_h = head[cell_id].frame.avg
        if avg_b <= 0.0:
            continue
        # Normalization is one-sided — only the head measurement is rescaled,
        # since the baseline was captured under its own (assumed clean)
        # conditions when it was committed.
        if use_normalized:
            avg_h = normalize_ms(avg_h, ref_ms, target_ms)
        if pct_delta(avg_b, avg_h) >= regress_pct:
            out.append(cell_id)
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", help="baseline root (docs/perf/baseline_latest/) — "
                                    "must contain <host-slug>/manifest.json for the head's slug")
    p.add_argument("head", help="head run directory")
    p.add_argument("--regress-pct", type=float, default=10.0,
                   help=">= this %% on mean frame avg counts as regression (default 10)")
    p.add_argument("--improve-pct", type=float, default=5.0,
                   help="<= negative this %% marks improvement in the table (default 5)")
    p.add_argument("--gpu-only", action="store_true")
    p.add_argument("--cpu-only", action="store_true")
    args = p.parse_args()

    if args.gpu_only and args.cpu_only:
        p.error("--gpu-only and --cpu-only are mutually exclusive")

    baseline_arg = Path(args.baseline).resolve()
    head_dir = Path(args.head).resolve()

    if not baseline_arg.is_dir():
        print(f"check_regression: baseline root not found: {baseline_arg}", file=sys.stderr)
        return 2
    if not head_dir.is_dir():
        print(f"check_regression: head dir not found: {head_dir}", file=sys.stderr)
        return 2

    head_manifest = load_manifest(head_dir)
    head = load_run(head_dir)
    if not head:
        print(f"check_regression: no cells in head {head_dir}", file=sys.stderr)
        return 2

    # No-baseline path → informational seed-new, pass.
    base_dir = resolve_baseline(baseline_arg, head_manifest)
    if base_dir is None:
        head_slug_str = host_slug(head_manifest) or "(none)"
        print(
            f"# Perf gate — seeding new baseline\n\n"
            f"No baseline at `{baseline_arg}/{head_slug_str}/`. The next master "
            f"push from this host will seed one. Gate is informational this PR.",
            end="",
        )
        print(
            f"check_regression: NO BASELINE — pass (informational, seed-new "
            f"path for slug {head_slug_str})",
            file=sys.stderr,
        )
        return 0

    base = load_run(base_dir)
    if not base:
        print(f"check_regression: no cells in baseline {base_dir}", file=sys.stderr)
        return 2

    base_manifest = load_manifest(base_dir)
    host_note = build_host_note(base_manifest, head_manifest)

    md = render_markdown(
        base_dir, head_dir, base, head,
        args.regress_pct, args.improve_pct,
        args.gpu_only, args.cpu_only,
        host_note=host_note,
    )
    print(md, end="")

    # Decide whether to gate.
    base_slug = host_slug(base_manifest)
    head_slug_str = host_slug(head_manifest)
    same_host = (
        base_slug
        and head_slug_str
        and base_slug == head_slug_str
    )
    if not same_host:
        print(
            f"check_regression: host mismatch — informational only (baseline "
            f"slug '{base_slug}', head slug '{head_slug_str}'); no gate fired.",
            file=sys.stderr,
        )
        return 0

    cal = head_manifest.get("calibration") or {}
    ref_ms = float(cal.get("ref_ms", 0.0))
    target_ms = float(cal.get("ref_target_ms", 0.0))
    lf = load_factor(ref_ms, target_ms)
    use_normalized = lf >= LOAD_FACTOR_TRUST_NORMALIZED

    regressed = _regressed_cells(
        base, head, args.regress_pct,
        ref_ms=ref_ms, target_ms=target_ms,
        use_normalized=use_normalized,
    )
    if regressed:
        weighting = "normalized" if use_normalized else "raw"
        print(
            f"check_regression: FAIL — {len(regressed)} cell(s) regressed "
            f">{args.regress_pct:.0f}% on {weighting} mean frame avg: "
            f"{', '.join(regressed)}",
            file=sys.stderr,
        )
        return 1

    print(
        "check_regression: PASS — no cells regressed "
        f"({'normalized' if use_normalized else 'raw'}, "
        f"load_factor {lf:.2f}×).",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
