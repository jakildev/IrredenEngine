#!/usr/bin/env bash
# Executed control for perf_grid_matrix.sh's per-cell load brackets: the
# three-sample `before` median, the single `after`, abort on a failed probe,
# and no bracket when the IR_PERF_CELL_REF_EXE seam is unset.
#
# Drives the real matrix script from a scratch engine root, with a stub
# fleet-run on PATH and a scripted reference probe. No build, no GPU.
# Usage: scripts/perf/tests/test_load_bracket_capture.sh

set -u

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../../.." && pwd)"
MATRIX="$REPO_ROOT/scripts/perf/perf_grid_matrix.sh"

LAB="$(mktemp -d "${TMPDIR:-/tmp}/perf-bracket.XXXXXX")"
trap 'rm -rf "$LAB"' EXIT

FAILURES=0
check () { # $1=arm  $2=condition-result(0/1)  $3=detail
  if [[ "$2" -eq 0 ]]; then
    echo "  PASS  $1: $3"
  else
    echo "  FAIL  $1: $3"
    FAILURES=$((FAILURES + 1))
  fi
}

# --- Fixtures -------------------------------------------------------------

ROOT="$LAB/root"
mkdir -p "$ROOT" "$LAB/bin"
touch "$ROOT/CMakePresets.json"
git init -q "$ROOT"
git -C "$ROOT" add -A
git -C "$ROOT" -c user.email=perf-test@example.com -c user.name="perf test" \
  commit -q -m root
SHA="$(git -C "$ROOT" rev-parse --short HEAD)"
export FLEET_ENGINE_ROOT="$ROOT"
export STUB_ENGINE_ROOT="$ROOT"

# One cell's run: a fresh profile report under the engine root's build tree,
# where the matrix script collects it.
cat > "$LAB/bin/fleet-run" <<'STUB'
#!/usr/bin/env bash
dir="$STUB_ENGINE_ROOT/build/stub/save_files"
mkdir -p "$dir"
printf 'Frame time: avg=1.0ms p50=1.0ms p95=1.0ms p99=1.0ms min=1.0ms max=1.0ms\n' \
  > "$dir/profile_report.txt"
STUB
chmod +x "$LAB/bin/fleet-run"
export PATH="$LAB/bin:$PATH"

# The reference probe: line N of $PROBE_SEQ on its Nth call, as ir_ref_bench's
# JSON; a `fail` line exits nonzero.
cat > "$LAB/probe" <<'PROBE'
#!/usr/bin/env bash
n=$(( $(cat "$PROBE_COUNT" 2>/dev/null || echo 0) + 1 ))
echo "$n" > "$PROBE_COUNT"
value="$(sed -n "${n}p" "$PROBE_SEQ")"
if [[ -z "$value" || "$value" == fail ]]; then
  echo "probe: scripted failure on call $n" >&2
  exit 1
fi
printf '{"ms": %s, "iterations": 1}\n' "$value"
PROBE
chmod +x "$LAB/probe"
export PROBE_SEQ="$LAB/probe.seq" PROBE_COUNT="$LAB/probe.count"

run_matrix () { # $1=label  $2=probe seam ("" = unset)  $3...=readings in call order
  local label="$1" seam="$2"
  shift 2
  printf '%s\n' "$@" > "$PROBE_SEQ"
  rm -f "$PROBE_COUNT"
  ( cd "$ROOT" && IR_PERF_CELL_REF_EXE="$seam" \
      "$MATRIX" --quick --frames 1 --label "$label" ) > "$LAB/$label.out" 2>&1
}

manifest () { echo "$ROOT/save_files/perf/$SHA-$1/manifest.json"; }
probe_calls () { cat "$PROBE_COUNT" 2>/dev/null || echo 0; }

# Prints each cell's load_ref_ms as compact JSON, one per line, or "none".
brackets () {
  python3 - "$1" <<'PY' | tr -d '\r'
import json, sys
for cell in json.load(open(sys.argv[1]))["cells"]:
    bracket = cell.get("load_ref_ms")
    print(json.dumps(bracket, sort_keys=True) if bracket is not None else "none")
PY
}

echo "perf_grid_matrix.sh load-bracket capture"

# --- A: three before samples per cell, median recorded, one after sample ---
run_matrix bracketed "$LAB/probe" 50 90 52 60  70 40 55 61; rc=$?
check A $rc "a bracketed --quick matrix exits 0"
[[ "$(probe_calls)" == 8 ]]
check A $? "two cells take exactly four probe readings each ($(probe_calls) calls)"
mapfile -t GOT < <(brackets "$(manifest bracketed)" 2>&1)
[[ "${GOT[0]:-}" == '{"after": 60.0, "before": 52.0, "before_samples": [50.0, 90.0, 52.0]}' ]]
check A $? "samples 50, 90, 52 record before 52 in call order (got ${GOT[0]:-nothing})"
[[ "${GOT[1]:-}" == '{"after": 61.0, "before": 55.0, "before_samples": [70.0, 40.0, 55.0]}' ]]
check A $? "the median is by value, not position (got ${GOT[1]:-nothing})"
grep -q 'load_ref_ms {"before": 52.0, "before_samples": \[50.0, 90.0, 52.0\], "after": 60.0}' \
  "$LAB/bracketed.out"
check A $? "the progress line carries the bracket"

# --- B: a probe failure on the second sample aborts before any bracket -----
run_matrix probe-fail "$LAB/probe" 50 fail 52 60; rc=$?
[[ $rc -ne 0 ]]
check B $? "a failed second sample fails the matrix (rc=$rc)"
[[ "$(probe_calls)" == 2 ]]
check B $? "the matrix stops at the failing sample ($(probe_calls) calls)"
grep -q "cell reference probe .* failed (before" "$LAB/probe-fail.out"
check B $? "the failure names the before probe"
if grep -q load_ref_ms "$(manifest probe-fail)" 2>/dev/null; then
  check B 1 "no bracket is written"
else
  check B 0 "no bracket is written"
fi
if grep -q '"id"' "$(manifest probe-fail)" 2>/dev/null; then
  check B 1 "no cell entry is written"
else
  check B 0 "no cell entry is written"
fi

# --- C: the seam unset writes no bracket and never runs the probe ----------
run_matrix unbracketed "" 50 50 50 50; rc=$?
check C $rc "an unbracketed --quick matrix exits 0"
[[ "$(probe_calls)" == 0 ]]
check C $? "the probe never runs ($(probe_calls) calls)"
mapfile -t GOT < <(brackets "$(manifest unbracketed)" 2>&1)
[[ "${GOT[*]:-}" == "none none" ]]
check C $? "both cells carry no load_ref_ms (got ${GOT[*]:-nothing})"

echo
if [[ $FAILURES -gt 0 ]]; then
  echo "$FAILURES assertion(s) failed"
  exit 1
fi
echo "all arms passed"
