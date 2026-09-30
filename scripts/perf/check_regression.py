#!/usr/bin/env python3
"""check_regression.py — fingerprint-aware regression gate over compare_perf_runs.

Loads the head run, resolves the matching fingerprinted baseline under the
provided baseline-root, prints the full compare_perf_runs markdown table
(with host-calibration block), then exits according to the gate decision
tree from #1074:

    Same fingerprint, lock uncontested → check raw deltas, fail on regression.
    Same fingerprint, lock contended   → check normalized deltas instead, the
                                         head rescaled onto the baseline run's
                                         own ref_ms (same SKU, so the ratio is
                                         load and nothing else).
    Different fingerprint              → informational only, pass.
    No baseline at all                 → informational only, pass (seed-new).

With --baseline-history (CI always passes it) the baseline is not the branch
tip but the most recent capture of the head's slug whose ref_ms sits inside
the LOAD_FACTOR_TRUST_NORMALIZED band of the head's:

    Class-matched capture found        → check raw deltas against it.
    No capture in the head's class     → informational only, pass, no table.

Usage:
    scripts/perf/check_regression.py <baseline_root> <head_dir>
        [--baseline-history DIR]
        [--regress-pct N] [--improve-pct N] [--gpu-only] [--cpu-only]

Exit codes:
    0   No regression above threshold (pass — may still show improvements)
    1   One or more cells regressed by more than --regress-pct (fail)
    2   Usage or measurement error
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_SCRIPTS_PERF = Path(__file__).resolve().parent
sys.path.insert(0, str(_SCRIPTS_PERF))
from compare_perf_runs import (  # noqa: E402
    CLASS_MATCH_MAX_AGE_DAYS,
    LOAD_FACTOR_TRUST_NORMALIZED,
    ClassMatch,
    build_host_note,
    calibration_ref_ms,
    host_slug,
    load_factor,
    load_manifest,
    load_run,
    normalize_ms,
    pct_delta,
    render_markdown,
    resolve_baseline,
    resolve_class_matched_baseline,
    unmeasured_cell_ids,
)


def _regressed_cells(base, head, regress_pct: float, *,
                     head_ref_ms: float, base_ref_ms: float,
                     use_normalized: bool) -> list[str]:
    out = []
    for cell_id in sorted(set(base) & set(head)):
        avg_b = base[cell_id].frame.avg
        avg_h = head[cell_id].frame.avg
        if avg_b <= 0.0:
            continue
        # Normalization is one-sided — only the head measurement is rescaled,
        # onto the machine-state the baseline was captured under. The
        # reference is the baseline's own ref_ms from the same SKU; scaling
        # against the fixed 50 ms target instead would divide out how slow the
        # SKU is, which no regression can change.
        if use_normalized:
            avg_h = normalize_ms(avg_h, head_ref_ms, base_ref_ms)
        if pct_delta(avg_b, avg_h) >= regress_pct:
            out.append(cell_id)
    return out


def _no_class_match_body(slug: str, head_ref_ms: float, match: ClassMatch) -> str:
    """The informational comment body. It carries no comparison table: a
    cross-class reading is meaningless, and a `↓` in it would earn the PR
    perf:improved."""
    lines = [
        "# Perf gate — no class-matched baseline",
        "",
        f"No capture of `{slug}` on the baseline branch is in this head's "
        f"calibration class (head ref_ms {head_ref_ms:.2f}; a capture qualifies "
        f"within {LOAD_FACTOR_TRUST_NORMALIZED:.2f}× of it, finished at most "
        f"{CLASS_MATCH_MAX_AGE_DAYS} days before this head started, same matrix "
        "and frame count, every cell measured). The next master push that lands on this runner "
        "class files one. Gate is informational this PR.",
    ]
    if match.captures:
        lines += ["", "Captures considered:", ""]
        lines += [f"- `{c.path.name}` ref_ms {c.ref_ms:.2f} — {c.rejected}"
                  for c in match.captures]
    return "\n".join(lines) + "\n"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", help="baseline root (docs/perf/baseline_latest/) — "
                                    "must contain <host-slug>/manifest.json for the head's slug")
    p.add_argument("head", help="head run directory")
    p.add_argument("--baseline-history", metavar="DIR",
                   help="per-capture history root (<slug>/<commit>/) — gate "
                        "against the class-matched capture instead of the tip")
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
    history_root = None
    if args.baseline_history:
        history_root = Path(args.baseline_history).resolve()
        if not history_root.is_dir():
            print(f"check_regression: baseline history not found: {history_root}",
                  file=sys.stderr)
            return 2

    head_manifest = load_manifest(head_dir)
    head = load_run(head_dir)
    if not head:
        print(f"check_regression: no cells in head {head_dir}", file=sys.stderr)
        return 2
    unmeasured_head = unmeasured_cell_ids(head)
    if unmeasured_head:
        print(
            "check_regression: unmeasured cells in head: "
            + ", ".join(unmeasured_head),
            file=sys.stderr,
        )
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

    capture = None
    capture_is_tip = False
    if history_root is not None:
        match = resolve_class_matched_baseline(history_root, head_manifest)
        if match.selected is None:
            head_slug_str = host_slug(head_manifest) or "(none)"
            head_ref_ms = calibration_ref_ms(head_manifest)
            print(_no_class_match_body(head_slug_str, head_ref_ms, match), end="")
            print(
                f"check_regression: NO CLASS-MATCHED BASELINE — pass "
                f"(informational, slug {head_slug_str}, head ref_ms "
                f"{head_ref_ms:.2f}, {len(match.captures)} capture(s) considered)",
                file=sys.stderr,
            )
            return 0
        capture = match.selected
        capture_is_tip = capture.manifest == load_manifest(base_dir)
        base_dir = capture.path

    base = load_run(base_dir)
    if not base:
        print(f"check_regression: no cells in baseline {base_dir}", file=sys.stderr)
        return 2
    unmeasured_base = unmeasured_cell_ids(base)
    if unmeasured_base:
        print(
            "check_regression: unmeasured cells in baseline: "
            + ", ".join(unmeasured_base),
            file=sys.stderr,
        )
        return 2

    base_manifest = load_manifest(base_dir)
    host_note = build_host_note(base_manifest, head_manifest,
                                capture=capture, capture_is_tip=capture_is_tip)

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

    head_cal = head_manifest.get("calibration") or {}
    base_cal = base_manifest.get("calibration") or {}
    head_ref_ms = float(head_cal.get("ref_ms", 0.0))
    # A baseline with no ref_ms (legacy manifest) leaves load_factor at 1.0,
    # so the gate reads raw deltas rather than rescaling against a reference
    # it does not have.
    base_ref_ms = float(base_cal.get("ref_ms", 0.0))
    lf = load_factor(head_ref_ms, base_ref_ms)
    # A class-matched capture is in band by construction; normalizing across
    # a class split under-corrects it, so history mode never does.
    use_normalized = capture is None and lf >= LOAD_FACTOR_TRUST_NORMALIZED

    regressed = _regressed_cells(
        base, head, args.regress_pct,
        head_ref_ms=head_ref_ms, base_ref_ms=base_ref_ms,
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
