#!/usr/bin/env bash
# jitter_probe_excursion_test.sh — pin the semantics of the per-axis
# --max-excursion-x / --max-excursion-y assertions (#2606).
#
# The default smooth-motion verdict models LINEAR motion, so a large but
# perfectly smooth centroid migration fits the line and scores clean. On a probe
# where one axis is supposed to stay PINNED while the other legitimately
# translates, that blind spot is the whole failure mode: neither shipped
# criterion (reversals, residual) can express "x stays pinned while y may
# translate". These fixtures are synthetic precisely so the contract is pinned
# independently of any render output — a live probe's populations move with the
# tree, this does not.
#
# Fixtures are two 8-frame 48x48 binary-PPM sequences of a 12x12 white square:
#   A — steps +2px/frame in x, y fixed  (14px x migration, 0px y)
#   B — the mirror: x fixed, +2px/frame in y
# stb_image decodes binary PPM, so no PNG encoder is needed.
#
# Nothing in CI runs test/tools/ (no CMakeLists, no workflow reference) — this
# is a hand/review artifact, so it SKIPs cleanly when the binary is absent
# rather than failing a tree that simply has not built the tool.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROBE="${JITTER_PROBE_BIN:-$REPO_ROOT/build/tools/jitter_probe/jitter_probe}"

if [[ ! -x "$PROBE" ]]; then
    echo "jitter_probe_excursion_test.sh: SKIP — no jitter_probe binary at '$PROBE'"
    echo "  build it with: fleet-build --target jitter_probe"
    echo "  or point JITTER_PROBE_BIN at one."
    exit 0
fi

FIXTURE_DIR="$(mktemp -d)"
trap 'rm -rf "$FIXTURE_DIR"' EXIT

python3 - "$FIXTURE_DIR" <<'PYEOF'
import os
import sys

out = sys.argv[1]
W = H = 48
SQ = 12
FRAMES = 8
STEP = 2
BASE = 4
FIXED = 18


def write_seq(name, moving_axis):
    d = os.path.join(out, name)
    os.makedirs(d, exist_ok=True)
    for i in range(FRAMES):
        moved = BASE + STEP * i
        x0, y0 = (moved, FIXED) if moving_axis == "x" else (FIXED, moved)
        px = bytearray(W * H * 3)
        for y in range(y0, y0 + SQ):
            row = y * W * 3
            for x in range(x0, x0 + SQ):
                o = row + x * 3
                px[o] = px[o + 1] = px[o + 2] = 255
        with open(os.path.join(d, "f%03d.ppm" % i), "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (W, H))
            f.write(bytes(px))


write_seq("A", "x")
write_seq("B", "y")
PYEOF

A=("$FIXTURE_DIR"/A/f*.ppm)
B=("$FIXTURE_DIR"/B/f*.ppm)

pass=0
fail=0
check() {
    local label="$1" cond="$2"
    if eval "$cond"; then
        echo "  PASS: $label"
        pass=$(( pass + 1 ))
    else
        echo "  FAIL: $label"
        fail=$(( fail + 1 ))
    fi
}

run_probe() {
    # Echoes the probe's stdout to $OUT and leaves its exit code in $rc.
    set +e
    OUT="$("$PROBE" "$@" 2>&1)"
    rc=$?
    set -e
}

echo "[1] sequence A, no excursion bar: the blind spot itself — a 14px smooth"
echo "    x migration still scores SMOOTH on the shipped criteria"
run_probe --reversal-eps 0.8 --expect-frames 8 "${A[@]}"
check "exit 0 (SMOOTH)" "[[ $rc -eq 0 ]]"
check "verdict line says SMOOTH" "grep -q 'verdict=SMOOTH' <<< \"\$OUT\""
# Without this the arm is vacuous: it would also pass on frames that never moved.
check "the 14px x migration IS present in the frames (excursion is reported)" \
    "grep -qE '^  x: .* excursion=14\.00px' <<< \"\$OUT\""

echo "[2] sequence A + --max-excursion-x 5: the gate fires on the same frames"
run_probe --reversal-eps 0.8 --expect-frames 8 --max-excursion-x 5 "${A[@]}"
check "exit 1 (JITTER)" "[[ $rc -eq 1 ]]"
check "verdict line says JITTER" "grep -q 'verdict=JITTER' <<< \"\$OUT\""
check "thresholds line names the bar that fired" \
    "grep -q 'max_excursion_x<=5.00px' <<< \"\$OUT\""

echo "[3] sequence B + --max-excursion-x 5: x pinned while y translates 14px"
echo "    — the contract #2606 exists for, unsayable with --stationary"
run_probe --reversal-eps 0.8 --expect-frames 8 --max-excursion-x 5 "${B[@]}"
check "exit 0 (SMOOTH)" "[[ $rc -eq 0 ]]"
# Non-vacuity: the pass must be because x held, not because nothing moved.
check "x excursion is 0.00px (pinned)" \
    "grep -qE '^  x: .* excursion=0\.00px' <<< \"\$OUT\""
check "y excursion is 14.00px (legitimately translating, unconstrained)" \
    "grep -qE '^  y: .* excursion=14\.00px' <<< \"\$OUT\""

echo "[4] sequence B + --max-excursion-y 5: axis independence — identical frames,"
echo "    mirrored assertion, opposite verdict"
run_probe --reversal-eps 0.8 --expect-frames 8 --max-excursion-y 5 "${B[@]}"
check "exit 1 (JITTER)" "[[ $rc -eq 1 ]]"
check "thresholds line names the y bar" \
    "grep -q 'max_excursion_y<=5.00px' <<< \"\$OUT\""

echo "[5] --stationary + an excursion bar is an argument error, not a silently"
echo "    ignored assertion"
run_probe --stationary --max-excursion-x 5 "${A[@]}"
check "exit 2 (argument error)" "[[ $rc -eq 2 ]]"
check "diagnostic explains the conflict" \
    "grep -q 'cannot be combined with --stationary' <<< \"\$OUT\""

echo "[6] --stationary alone is unchanged (pivot-verify.py parses this output)"
run_probe --stationary --max-deviation 1.5 "${A[@]}"
check "exit 1 (DRIFT — the square does move)" "[[ $rc -eq 1 ]]"
check "summary still carries the max_deviation lines pivot-verify.py regexes" \
    "grep -qE '^  x: max_deviation=[0-9.]+px' <<< \"\$OUT\""
check "no excursion field leaked into the --stationary summary" \
    "! grep -q 'excursion' <<< \"\$OUT\""

echo
echo "jitter_probe_excursion_test.sh: $pass passed, $fail failed"
exit "$fail"
