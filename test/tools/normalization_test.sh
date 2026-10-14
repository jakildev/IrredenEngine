#!/usr/bin/env bash
# normalization_test.sh — exercises compare_perf_runs's calibration helpers
# (normalize_ms, load_factor, host_slug, resolve_baseline, build_host_note),
# the per-cell load gate (load_flagged_cells), and the gate decision tree in
# check_regression.py.
#
# Pure stdlib python — no engine build needed. Synthesizes baseline + head
# manifest.json fixtures under a temp dir and asserts the comparator picks
# the right code path.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SCRIPTS_PERF="$REPO_ROOT/scripts/perf"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
fail=0
check() {
    local label="$1" cond="$2"
    if eval "$cond"; then
        echo "  PASS: $label"
        pass=$((pass + 1))
    else
        echo "  FAIL: $label"
        fail=$((fail + 1))
    fi
}

# ---------------------------------------------------------------------------
# [1] Math helpers
# ---------------------------------------------------------------------------

echo "[1] normalize_ms / load_factor math"
python3 - "$SCRIPTS_PERF" <<'PY' > "$WORK/math.out"
import sys
sys.path.insert(0, sys.argv[1])
from compare_perf_runs import normalize_ms, load_factor, LOAD_FACTOR_TRUST_NORMALIZED

# Identity: ref == target → no scaling.
assert normalize_ms(10.0, 50.0, 50.0) == 10.0, "identity"

# Load factor 2× → measurement halved (10ms becomes 5ms normalized).
assert normalize_ms(10.0, 100.0, 50.0) == 5.0, "2x load halves"

# Load factor 0.5× (host faster than calibration) → measurement doubled.
assert normalize_ms(10.0, 25.0, 50.0) == 20.0, "0.5x load doubles"

# Zero / negative ref → no-op.
assert normalize_ms(10.0, 0.0, 50.0) == 10.0, "zero ref no-op"
assert normalize_ms(10.0, -1.0, 50.0) == 10.0, "negative ref no-op"
assert normalize_ms(10.0, 50.0, 0.0) == 10.0, "zero target no-op"

# Load factor.
assert load_factor(50.0, 50.0) == 1.0, "lf=1 uncontested"
assert load_factor(100.0, 50.0) == 2.0, "lf=2 loaded"
assert load_factor(25.0, 50.0) == 0.5, "lf=0.5 fast"

# The gate's call convention: (head ref, BASELINE ref), both from the same
# SKU. A slow SKU measured twice at rest reads uncontested however far its
# ref sits from the global 50 ms target.
assert load_factor(104.4, 104.4) == 1.0, "slow SKU at rest is uncontested"
assert normalize_ms(12.0, 104.4, 104.4) == 12.0, "slow SKU at rest is unscaled"
assert abs(load_factor(208.8, 104.4) - 2.0) < 1e-9, "same SKU, 2x loaded"
assert abs(normalize_ms(12.0, 208.8, 104.4) - 6.0) < 1e-9, "2x load halves the head"
# A baseline with no ref_ms (legacy manifest) must fall back to raw deltas.
assert load_factor(104.4, 0.0) == 1.0, "missing baseline ref -> raw"

# Threshold sanity.
assert LOAD_FACTOR_TRUST_NORMALIZED >= 1.0
print("ok")
PY

check "math helpers produce expected values" '[[ "$(cat "$WORK/math.out")" == "ok" ]]'

# ---------------------------------------------------------------------------
# [2] resolve_baseline picks the matching fingerprint subdir
# ---------------------------------------------------------------------------

echo "[2] resolve_baseline fingerprint matching"
mkdir -p "$WORK/baselines/linux-x86_64-foo"
cat > "$WORK/baselines/linux-x86_64-foo/manifest.json" <<'EOF'
{"cells": [], "calibration": {"host_slug": "linux-x86_64-foo"}}
EOF

mkdir -p "$WORK/baselines/macos-arm64-bar"
cat > "$WORK/baselines/macos-arm64-bar/manifest.json" <<'EOF'
{"cells": [], "calibration": {"host_slug": "macos-arm64-bar"}}
EOF

python3 - "$SCRIPTS_PERF" "$WORK" <<'PY' > "$WORK/resolve.out"
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from compare_perf_runs import resolve_baseline

baselines = Path(sys.argv[2]) / "baselines"

# Matching head slug → matching dir.
got = resolve_baseline(baselines, {"calibration": {"host_slug": "macos-arm64-bar"}})
assert got is not None and got.name == "macos-arm64-bar", got

# Unknown head slug → no match (no flat layout either).
got = resolve_baseline(baselines, {"calibration": {"host_slug": "windows-foo-bar"}})
assert got is None, got

# Head has no calibration → no fingerprint, no flat layout → None.
got = resolve_baseline(baselines, {})
assert got is None, got

print("ok")
PY
check "resolve_baseline returns correct fingerprint subdir" '[[ "$(cat "$WORK/resolve.out")" == "ok" ]]'

# Legacy flat layout: baseline_root has manifest.json directly.
mkdir -p "$WORK/legacy"
cat > "$WORK/legacy/manifest.json" <<'EOF'
{"cells": []}
EOF
python3 - "$SCRIPTS_PERF" "$WORK" <<'PY' > "$WORK/resolve_legacy.out"
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
from compare_perf_runs import resolve_baseline
legacy = Path(sys.argv[2]) / "legacy"
# Head without calibration → falls through to the legacy path.
got = resolve_baseline(legacy, {})
assert got == legacy, got
print("ok")
PY
check "resolve_baseline falls back to legacy flat layout" \
      '[[ "$(cat "$WORK/resolve_legacy.out")" == "ok" ]]'

# ---------------------------------------------------------------------------
# [3] build_host_note text
# ---------------------------------------------------------------------------

echo "[3] build_host_note classification"
python3 - "$SCRIPTS_PERF" <<'PY' > "$WORK/note.out"
import sys
sys.path.insert(0, sys.argv[1])
from compare_perf_runs import build_host_note

# Same host, lock uncontested.
note = build_host_note(
    {"calibration": {"host_slug": "a", "ref_ms": 50.0}},
    {"calibration": {"host_slug": "a", "ref_ms": 50.0, "ref_target_ms": 50.0}},
)
assert "matches baseline" in note, note
assert "raw (lock uncontested)" in note, note

# Same host, loaded relative to the BASELINE's reading.
note = build_host_note(
    {"calibration": {"host_slug": "a", "ref_ms": 50.0}},
    {"calibration": {"host_slug": "a", "ref_ms": 100.0, "ref_target_ms": 50.0}},
)
assert "matches baseline" in note, note
assert "normalized over raw" in note, note
assert "head 100.00 vs baseline 50.00" in note, note

# A slow SKU measured twice at rest: both refs far above the 50 ms target,
# but equal to each other. Weighing against the target would call this
# "loaded" forever; against the baseline's own ref it reads uncontested.
note = build_host_note(
    {"calibration": {"host_slug": "a", "ref_ms": 104.4}},
    {"calibration": {"host_slug": "a", "ref_ms": 104.4, "ref_target_ms": 50.0}},
)
assert "raw (lock uncontested)" in note, note
assert "informational" in note, note

# A baseline with no ref_ms (legacy manifest) falls back to raw.
note = build_host_note(
    {"calibration": {"host_slug": "a"}},
    {"calibration": {"host_slug": "a", "ref_ms": 100.0, "ref_target_ms": 50.0}},
)
assert "raw (lock uncontested)" in note, note

# Different host.
note = build_host_note(
    {"calibration": {"host_slug": "a", "ref_ms": 50.0}},
    {"calibration": {"host_slug": "b", "ref_ms": 50.0, "ref_target_ms": 50.0}},
)
assert "host mismatch" in note, note
assert "informational only" in note, note

# No calibration anywhere → empty.
assert build_host_note({}, {}) == ""

print("ok")
PY
check "host-note classification covers all branches" \
      '[[ "$(cat "$WORK/note.out")" == "ok" ]]'

# ---------------------------------------------------------------------------
# [4] check_regression: end-to-end gate decision
# ---------------------------------------------------------------------------

echo "[4] check_regression gate decisions"

write_run() {
    local dir="$1" slug="$2" frame_ms="$3" ref_ms="$4"
    mkdir -p "$dir"
    cat > "$dir/manifest.json" <<EOF
{
  "cells": [
    {"id": "smoke", "target": "IRPerfGrid", "report": "smoke.txt"}
  ],
  "calibration": {
    "host_slug": "$slug",
    "ref_ms": $ref_ms,
    "ref_target_ms": 50.0,
    "host_fingerprint": {"slug": "$slug"},
    "math_sha": "deadbeef"
  }
}
EOF
    cat > "$dir/smoke.txt" <<EOF
Frame time: avg=${frame_ms}ms p50=${frame_ms}ms p95=${frame_ms}ms p99=${frame_ms}ms min=0.5ms max=99.0ms
Entity count: 1 (1 archetypes)
=== END REPORT
EOF
}

# Scenario A: same host, uncontested, head matches baseline → PASS.
write_run "$WORK/baselines2/a-slug" "a-slug" 10.0 50.0
write_run "$WORK/head_clean" "a-slug" 10.0 50.0

set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines2" "$WORK/head_clean" >"$WORK/A.out" 2>"$WORK/A.err"
A_STATUS=$?
set -e
check "A: same host clean → exit 0" "[[ $A_STATUS -eq 0 ]]"
check "A: 'PASS' on stderr"          'grep -q "PASS" "$WORK/A.err"'

# Scenario B: same host, head regressed >10% on raw → FAIL.
write_run "$WORK/head_regress_raw" "a-slug" 12.0 50.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines2" "$WORK/head_regress_raw" >"$WORK/B.out" 2>"$WORK/B.err"
B_STATUS=$?
set -e
check "B: raw regression >10% → exit 1" "[[ $B_STATUS -eq 1 ]]"
check "B: 'FAIL' on stderr"             'grep -q "FAIL" "$WORK/B.err"'

# Scenario C: same host, head's raw +15% but the head's ref is 3× the BASELINE's
# ref, so the normalized head lands well below the 10% threshold.
write_run "$WORK/head_loaded_no_regress" "a-slug" 11.5 150.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines2" "$WORK/head_loaded_no_regress" >"$WORK/C.out" 2>"$WORK/C.err"
C_STATUS=$?
set -e
# baseline ref 50 (baselines2/a-slug), head ref 150 → load_factor 3.00×;
# head avg → normalize_ms(11.5, 150.0, 50.0) = 11.5*(50/150) ≈ 3.83ms → -61.7% vs 10ms baseline → gate passes
check "C: loaded host normalized below threshold → exit 0" "[[ $C_STATUS -eq 0 ]]"
check "C: stderr cites 'normalized'" 'grep -q "normalized" "$WORK/C.err"'

# Scenario C2 [positive fire]: a SLOW host measured at rest on both sides.
# Both refs are 100 ms against a 50 ms calibration target, so the pre-D4 gate
# read load_factor 2.00×, halved the head, and turned a real +20% into -40%.
# Against the baseline's own ref the load factor is 1.00× and the regression
# lands raw.
write_run "$WORK/baselines_slow/a-slug" "a-slug" 10.0 100.0
write_run "$WORK/head_slow_regress" "a-slug" 12.0 100.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines_slow" "$WORK/head_slow_regress" >"$WORK/C2.out" 2>"$WORK/C2.err"
C2_STATUS=$?
set -e
check "C2: slow host at rest, +20% head → exit 1" "[[ $C2_STATUS -eq 1 ]]"
check "C2: stderr names the raw weighting" 'grep -q "on raw mean frame avg" "$WORK/C2.err"'
check "C2: the host note reports both refs" \
      'grep -q "head 100.00 vs baseline 100.00" "$WORK/C2.out"'

# Scenario D: different host (no matching subdir, no legacy flat) → seed-new.
write_run "$WORK/head_other_host" "b-slug" 99.0 50.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines2" "$WORK/head_other_host" >"$WORK/D.out" 2>"$WORK/D.err"
D_STATUS=$?
set -e
check "D: unknown slug → exit 0 (informational)" "[[ $D_STATUS -eq 0 ]]"
check "D: stderr cites 'NO BASELINE'" 'grep -q "NO BASELINE" "$WORK/D.err"'

# Scenario D2: legacy flat baseline + head with mismatching slug → "host mismatch".
mkdir -p "$WORK/legacy_baseline"
cat > "$WORK/legacy_baseline/manifest.json" <<'EOF'
{
  "cells": [
    {"id": "smoke", "target": "IRPerfGrid", "report": "smoke.txt"}
  ],
  "calibration": {"host_slug": "a-slug"}
}
EOF
cat > "$WORK/legacy_baseline/smoke.txt" <<'EOF'
Frame time: avg=10.0ms p50=10.0ms p95=10.0ms p99=10.0ms min=0.5ms max=99.0ms
Entity count: 1 (1 archetypes)
=== END REPORT
EOF
write_run "$WORK/head_against_legacy" "b-slug" 99.0 50.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/legacy_baseline" "$WORK/head_against_legacy" >"$WORK/D2.out" 2>"$WORK/D2.err"
D2_STATUS=$?
set -e
check "D2: legacy flat + slug mismatch → exit 0 (informational)" "[[ $D2_STATUS -eq 0 ]]"
check "D2: stderr cites 'host mismatch'" 'grep -q "host mismatch" "$WORK/D2.err"'

# Scenario E: empty baseline-root (no subdirs at all) → exit 0 informational.
mkdir -p "$WORK/empty_baselines"
write_run "$WORK/head_orphan" "lone-slug" 10.0 50.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/empty_baselines" "$WORK/head_orphan" >"$WORK/E.out" 2>"$WORK/E.err"
E_STATUS=$?
set -e
check "E: empty baselines root → exit 0 (seed-new)" "[[ $E_STATUS -eq 0 ]]"
check "E: stderr cites 'NO BASELINE'" 'grep -q "NO BASELINE" "$WORK/E.err"'

# ---------------------------------------------------------------------------
# [5] Per-cell load gate: load_ref_ms.before against the run floor
# ---------------------------------------------------------------------------

echo "[5] load gate classification and grading"

HOSTED="linux-x86_64-epyc-7763-64-unknown"
DARWIN="darwin-arm64-apple-m2-max"
WSL_NO_GPU="linux-x86_64-ryzen-9-5950x-unknown"

# write_bracketed_run <dir> <slug> <start ref_ms> <cell>...
# Each cell is <id>:<frame avg ms>:<before|-|bad>[:<after>]; "-" writes no
# bracket, "bad" writes a non-numeric before.
write_bracketed_run() {
    python3 - "$@" <<'PY'
import json, sys
from pathlib import Path
run, slug, start = Path(sys.argv[1]), sys.argv[2], float(sys.argv[3])
run.mkdir(parents=True, exist_ok=True)
cells = []
for spec in sys.argv[4:]:
    cell_id, avg, before, *after = spec.split(":")
    cell = {"id": cell_id, "target": "IRPerfGrid", "report": f"{cell_id}.txt"}
    if before != "-":
        bracket = {"before": before if before == "bad" else float(before)}
        if after:
            bracket["after"] = float(after[0])
        cell["load_ref_ms"] = bracket
    cells.append(cell)
    (run / f"{cell_id}.txt").write_text(
        f"Frame time: avg={avg}ms p50={avg}ms p95={avg}ms p99={avg}ms min=0.5ms max=99.0ms\n"
        "Entity count: 1 (1 archetypes)\n=== END REPORT\n")
manifest = {"cells": cells, "calibration": {
    "host_slug": slug, "ref_ms": start, "ref_target_ms": 50.0,
    "host_fingerprint": {"slug": slug}, "math_sha": "deadbeef"}}
(run / "manifest.json").write_text(json.dumps(manifest))
PY
}

# gate <case> <baseline root> <head dir> [flags...] → $WORK/<case>.{out,err,rc}
gate() {
    local name="$1" root="$2" head="$3"
    shift 3
    set +e
    python3 "$SCRIPTS_PERF/check_regression.py" "$root" "$head" "$@" \
        >"$WORK/$name.out" 2>"$WORK/$name.err"
    echo $? > "$WORK/$name.rc"
    set -e
}
rc_of() { tr -d '\r' < "$WORK/$1.rc"; }

python3 - "$SCRIPTS_PERF" <<'PY' > "$WORK/classify.out"
import sys
sys.path.insert(0, sys.argv[1])
from compare_perf_runs import LOAD_GATE_THRESHOLDS, load_flagged_cells, load_gate_threshold

def run(slug, start, *befores):
    return {"calibration": {"host_slug": slug, "ref_ms": start},
            "cells": [{"id": f"c{i}", "load_ref_ms": {"before": b, "after": 999.0}}
                      for i, b in enumerate(befores, 1)]}

def flagged(manifest):
    return [(f.cell_id, round(f.factor, 4)) for f in load_flagged_cells(manifest, "head")]

# Every hosted slug ever written to perf-baseline is in scope; nothing else is.
for slug in ("linux-x86_64-epyc-7763-64-unknown", "linux-x86_64-epyc-9v45-96-unknown",
             "linux-x86_64-epyc-9v74-80-unknown", "linux-x86_64-xeon-6973p-c-unknown",
             "linux-x86_64-xeon-platinum-8370c-unknown",
             "linux-x86_64-xeon-platinum-8573c-unknown"):
    assert load_gate_threshold(slug) is not None, slug
for slug in ("darwin-arm64-apple-m2-max", "macos-arm64-apple-m2",
             "linux-x86_64-ryzen-9-5950x-unknown", "linux-x86_64-epyc-7763-64-nvidia-a10",
             "windows-x86_64-ryzen-9-5950x-rtx-4090", ""):
    assert load_gate_threshold(slug) is None, slug
assert not any(p.startswith(("darwin", "macos")) for p in LOAD_GATE_THRESHOLDS)
assert all(t <= 1.20 for t in LOAD_GATE_THRESHOLDS.values())

hosted = "linux-x86_64-epyc-7763-64-unknown"
# Start 70, befores 50 and 62: the floor is 50, so the second cell is 1.24x.
assert flagged(run(hosted, 70.0, 50.0, 62.0)) == [("c2", 1.24)], flagged(run(hosted, 70.0, 50.0, 62.0))
# Load present at the start lowers nothing: the floor is the quietest reading.
assert flagged(run(hosted, 50.0, 52.0, 51.0)) == [], "under threshold"
# Inclusive boundary on an exact quotient: 55 / 50 == 1.10.
assert flagged(run(hosted, 50.0, 55.0)) == [("c1", 1.1)]
assert flagged(run(hosted, 50.0, 54.99)) == []
# A missing, non-positive or non-numeric before is not classified and does not
# pull the floor down.
assert flagged(run(hosted, 50.0, 0.0, -3.0, "x", None, 60.0)) == [("c5", 1.2)]
m = run(hosted, 50.0, 60.0); del m["cells"][0]["load_ref_ms"]
assert flagged(m) == []
# after is never read.
assert flagged(run(hosted, 50.0, 50.0)) == []
print("ok")
PY
check "classifier: scope table, floor, inclusive boundary, bad brackets" \
      '[[ "$(tr -d "\r" < "$WORK/classify.out")" == "ok" ]]'

# Baseline: two cells at 10 ms, quiet brackets.
write_bracketed_run "$WORK/lg_base/$HOSTED" "$HOSTED" 50 c1:10.0:50.0:52 c2:10.0:50.0:52

# G1: in-scope, regressed cell with before at exactly T x floor → informational.
write_bracketed_run "$WORK/lg_head_flagged" "$HOSTED" 50 c1:12.0:55.0:52 c2:10.0:50.0:52
gate G1 "$WORK/lg_base" "$WORK/lg_head_flagged"
check "G1: flagged regressed cell → exit 0" '[[ "$(rc_of G1)" == 0 ]]'
check "G1: stderr names c1 as load-flagged" 'grep -q "load-flagged cell(s) informational, not graded: c1 (head 1.100×" "$WORK/G1.err"'
check "G1: markdown lists the flagged cell" 'grep -q "^| \`c1\` | head | 55.00 | 50.00 | 1.100× | 1.10× |" "$WORK/G1.out"'
check "G1: frame row carries the marker and no mark" \
      'grep "^| \`c1\` (load-flagged) |" "$WORK/G1.out" | grep -vq "⚠\|↓"'

# G2: the same regression with a clean bracket → exit 1.
write_bracketed_run "$WORK/lg_head_clean" "$HOSTED" 50 c1:12.0:50.0:52 c2:10.0:50.0:52
gate G2 "$WORK/lg_base" "$WORK/lg_head_clean"
check "G2: clean-bracket regression → exit 1" '[[ "$(rc_of G2)" == 1 ]]'
check "G2: no load finding" '! grep -q "load-flagged" "$WORK/G2.out" "$WORK/G2.err"'

# G3: macOS never grades on brackets — 1.4x before and after on a regressed cell.
write_bracketed_run "$WORK/lg_mac_base/$DARWIN" "$DARWIN" 50 c1:10.0:50.0:52
write_bracketed_run "$WORK/lg_mac_regress" "$DARWIN" 50 c1:12.0:70.0:70
gate G3 "$WORK/lg_mac_base" "$WORK/lg_mac_regress"
check "G3: darwin regression with 1.4x brackets → exit 1" '[[ "$(rc_of G3)" == 1 ]]'
check "G3: no load finding" '! grep -q "load-flagged" "$WORK/G3.out" "$WORK/G3.err"'
write_bracketed_run "$WORK/lg_mac_clean" "$DARWIN" 50 c1:10.0:70.0:70
gate G3b "$WORK/lg_mac_base" "$WORK/lg_mac_clean"
check "G3b: same darwin brackets, no regression → exit 0" '[[ "$(rc_of G3b)" == 0 ]]'
check "G3b: no load finding" '! grep -q "load-flagged" "$WORK/G3b.out" "$WORK/G3b.err"'

# G4: in scope, 1.4x after only → not flagged.
write_bracketed_run "$WORK/lg_head_after" "$HOSTED" 50 c1:12.0:50.0:70 c2:10.0:50.0:70
gate G4 "$WORK/lg_base" "$WORK/lg_head_after"
check "G4: 1.4x after only → graded, exit 1" '[[ "$(rc_of G4)" == 1 ]]'
check "G4: no load finding" '! grep -q "load-flagged" "$WORK/G4.out" "$WORK/G4.err"'

# G5: flagged on the baseline side only → informational.
write_bracketed_run "$WORK/lg_base_loaded/$HOSTED" "$HOSTED" 50 c1:10.0:70.0:52 c2:10.0:50.0:52
gate G5 "$WORK/lg_base_loaded" "$WORK/lg_head_clean"
check "G5: baseline-side flag → exit 0" '[[ "$(rc_of G5)" == 0 ]]'
check "G5: stderr names the baseline side" 'grep -q "c1 (baseline 1.400×" "$WORK/G5.err"'

# G6: one flagged and one unflagged regressed cell → exit 1 naming only c2.
write_bracketed_run "$WORK/lg_head_mixed" "$HOSTED" 50 c1:12.0:70.0:52 c2:12.0:50.0:52
gate G6 "$WORK/lg_base" "$WORK/lg_head_mixed"
check "G6: unflagged regression still fails → exit 1" '[[ "$(rc_of G6)" == 1 ]]'
check "G6: FAIL names only c2" \
      'grep "FAIL — 1 cell(s) regressed" "$WORK/G6.err" | grep -q ": c2" && ! grep "FAIL" "$WORK/G6.err" | grep -q "c1"'

# G7: a missing bracket and a non-positive before are graded as today.
write_bracketed_run "$WORK/lg_head_nobracket" "$HOSTED" 50 c1:12.0:- c2:10.0:-
gate G7 "$WORK/lg_base" "$WORK/lg_head_nobracket"
check "G7: missing bracket regression → exit 1" '[[ "$(rc_of G7)" == 1 ]]'
write_bracketed_run "$WORK/lg_head_zero" "$HOSTED" 50 c1:12.0:0 c2:10.0:bad
gate G7b "$WORK/lg_base" "$WORK/lg_head_zero"
check "G7b: non-positive / malformed before regression → exit 1" '[[ "$(rc_of G7b)" == 1 ]]'
check "G7b: no load finding" '! grep -q "load-flagged" "$WORK/G7b.out" "$WORK/G7b.err"'

# G8: a GPU-less Linux host outside the hosted SKU families stays inert.
write_bracketed_run "$WORK/lg_wsl_base/$WSL_NO_GPU" "$WSL_NO_GPU" 50 c1:10.0:50.0:52
write_bracketed_run "$WORK/lg_wsl_head" "$WSL_NO_GPU" 50 c1:12.0:70.0:52
gate G8 "$WORK/lg_wsl_base" "$WORK/lg_wsl_head"
check "G8: ryzen-unknown 1.4x before on a regressed cell → exit 1" '[[ "$(rc_of G8)" == 1 ]]'
check "G8: no load finding" '! grep -q "load-flagged" "$WORK/G8.out" "$WORK/G8.err"'

# G9: --strict-load on a flagged run → exit 2, no table, no verdict.
gate G9 "$WORK/lg_base" "$WORK/lg_head_flagged" --strict-load
check "G9: --strict-load on a flagged head → exit 2" '[[ "$(rc_of G9)" == 2 ]]'
check "G9: no table on stdout" '[[ ! -s "$WORK/G9.out" ]]'
check "G9: stderr lists the flagged cell, no verdict" \
      'grep -q "LOAD-FLAGGED — 1 cell(s) .*c1 (head" "$WORK/G9.err" && ! grep -q "PASS\|FAIL" "$WORK/G9.err"'
gate G9b "$WORK/lg_base_loaded" "$WORK/lg_head_clean" --strict-load
check "G9b: --strict-load on a flagged baseline → exit 2" '[[ "$(rc_of G9b)" == 2 ]]'
gate G9c "$WORK/lg_base" "$WORK/lg_head_clean" --strict-load
check "G9c: --strict-load with nothing flagged grades as usual (exit 1)" '[[ "$(rc_of G9c)" == 1 ]]'

# G10: a flagged baseline cell whose head reads faster carries no ↓.
write_bracketed_run "$WORK/lg_base_slow/$HOSTED" "$HOSTED" 50 c1:20.0:70.0:52 c2:10.0:50.0:52
gate G10 "$WORK/lg_base_slow" "$WORK/lg_head_clean"
check "G10: no ↓ anywhere for a flagged cell" '! grep -q "↓" "$WORK/G10.out"'

echo
echo "normalization_test.sh: $pass passed, $fail failed"
exit "$fail"
