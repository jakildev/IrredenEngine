#!/usr/bin/env bash
# Tests for the operator/test entry-point case block's catch-all arm (#2962):
# an unrecognized first argument (or a one-char typo of a real flag) must exit
# non-zero WITHOUT falling through into main() and starting the daemon loop.
#
# The daemon loop never returns, so this can't be probed by invoking the real
# fleet-dispatcher with a bad flag directly — a regression would hang the
# test. Instead, copy the script and replace the trailing `main "$@"` with a
# marker (the same technique #2962's repro used), so a fall-through is
# observable as output instead of a hang.

set -uo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$SCRIPT_DIR/tests/lib_assert.sh"
DISPATCHER="$SCRIPT_DIR/fleet-dispatcher"
[[ -x "$DISPATCHER" ]] || { echo "test setup: fleet-dispatcher not found"; exit 1; }

TMPROOT=""; cleanup(){ [[ -n "$TMPROOT" && -d "$TMPROOT" ]] && rm -rf "$TMPROOT"; }
trap cleanup EXIT
TMPROOT=$(mktemp -d)
PROBE="$TMPROOT/fd-probe.sh"

cp "$DISPATCHER" "$PROBE"
python3 - "$PROBE" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
tail = 'main "$@"'
body = s.rstrip()
assert body.endswith(tail), "fleet-dispatcher no longer ends in main \"$@\" -- update this test's probe"
open(p, "w").write(body[: -len(tail)] + 'echo "REACHED_MAIN argv=$*"\n')
PY
chmod +x "$PROBE"

run() { "$PROBE" "$@"; echo "rc=$?"; }

echo "T1: --print-cap worker (positive control) -> prints cap, rc=0, no REACHED_MAIN"
out=$(run --print-cap worker)
assert_absent "$out" "REACHED_MAIN" "--print-cap worker does not reach main"
assert_contains "$out" "rc=0" "--print-cap worker exits 0"

echo "T2: --print-cap (usage-error control) -> usage message, rc=2, no REACHED_MAIN"
out=$(run --print-cap 2>&1)
assert_absent "$out" "REACHED_MAIN" "--print-cap (no role) does not reach main"
assert_contains "$out" "rc=2" "--print-cap (no role) exits 2"

echo "T3: --bogus-flag -> unrecognized, rc=2, no REACHED_MAIN"
out=$(run --bogus-flag 2>&1)
assert_absent "$out" "REACHED_MAIN" "--bogus-flag does not reach main"
assert_contains "$out" "rc=2" "--bogus-flag exits 2"

echo "T4: --print-caps worker (one-char typo) -> unrecognized, rc=2, no REACHED_MAIN"
out=$(run --print-caps worker 2>&1)
assert_absent "$out" "REACHED_MAIN" "--print-caps typo does not reach main"
assert_contains "$out" "rc=2" "--print-caps typo exits 2"

echo "T5: status (bare word) -> unrecognized, rc=2, no REACHED_MAIN"
out=$(run status 2>&1)
assert_absent "$out" "REACHED_MAIN" "bare 'status' does not reach main"
assert_contains "$out" "rc=2" "bare 'status' exits 2"

echo "T6: no argument -> must still reach main (the daemon boot path)"
out=$(run)
assert_contains "$out" "REACHED_MAIN argv=" "bare invocation still reaches main"
assert_contains "$out" "rc=0" "bare invocation exits 0"

summarize "fleet-dispatcher arg catch-all"
