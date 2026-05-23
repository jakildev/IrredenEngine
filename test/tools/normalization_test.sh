#!/usr/bin/env bash
# normalization_test.sh — exercises compare_perf_runs's calibration helpers
# (normalize_ms, load_factor, host_slug, resolve_baseline, build_host_note)
# and the gate decision tree in check_regression.py.
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
    {"calibration": {"host_slug": "a"}},
    {"calibration": {"host_slug": "a", "ref_ms": 50.0, "ref_target_ms": 50.0}},
)
assert "matches baseline" in note, note
assert "raw (lock uncontested)" in note, note

# Same host, loaded.
note = build_host_note(
    {"calibration": {"host_slug": "a"}},
    {"calibration": {"host_slug": "a", "ref_ms": 100.0, "ref_target_ms": 50.0}},
)
assert "matches baseline" in note, note
assert "normalized over raw" in note, note

# Different host.
note = build_host_note(
    {"calibration": {"host_slug": "a"}},
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
mv "$WORK/baselines2/a-slug/smoke.txt" "$WORK/baselines2/a-slug/smoke.txt"

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

# Scenario C: same host, head's *raw* +20% but ref_ms 1.5× → normalized = +20%/1.5 = ~+13%, still fails.
# But if ref_ms is 2× target, normalized = +20%/2 = +10% — at threshold, also fails.
# Use ref=3× target to push normalized below 10% threshold.
write_run "$WORK/head_loaded_no_regress" "a-slug" 11.5 150.0
set +e
python3 "$SCRIPTS_PERF/check_regression.py" \
    "$WORK/baselines2" "$WORK/head_loaded_no_regress" >"$WORK/C.out" 2>"$WORK/C.err"
C_STATUS=$?
set -e
# raw delta = +15%, normalized delta = +15% * (50/150) = -61.7%
check "C: loaded host normalized below threshold → exit 0" "[[ $C_STATUS -eq 0 ]]"
check "C: stderr cites 'normalized'" 'grep -q "normalized" "$WORK/C.err"'

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

echo
echo "normalization_test.sh: $pass passed, $fail failed"
exit "$fail"
