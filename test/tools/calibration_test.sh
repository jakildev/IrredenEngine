#!/usr/bin/env bash
# calibration_test.sh — sanity-check ir-host-probe.
#
# Two requirements: parseable JSON, deterministic across consecutive calls.
# The actual host values aren't asserted — they vary per machine.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
IR_PROBE="$REPO_ROOT/engine/tools/bin/ir-host-probe"

# Isolate the cache so we don't trample a real fingerprint sidecar.
export XDG_CACHE_HOME
XDG_CACHE_HOME="$(mktemp -d)"
trap 'rm -rf "$XDG_CACHE_HOME"' EXIT

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

echo "[1] probe emits valid JSON"
out1="$("$IR_PROBE" --refresh)"
check "valid JSON" 'python3 -c "import json, sys; json.loads(sys.argv[1])" "$out1"'

echo "[2] required keys present"
check "has slug"         '[[ -n "$(echo "$out1" | python3 -c "import json,sys; print(json.load(sys.stdin)[\"slug\"])")" ]]'
check "has cpu.model"    '[[ -n "$(echo "$out1" | python3 -c "import json,sys; print(json.load(sys.stdin)[\"cpu\"][\"model\"])")" ]]'
check "has gpu.model"    '[[ -n "$(echo "$out1" | python3 -c "import json,sys; print(json.load(sys.stdin)[\"gpu\"][\"model\"])")" ]]'
check "has os.kernel"    '[[ -n "$(echo "$out1" | python3 -c "import json,sys; print(json.load(sys.stdin)[\"os\"][\"kernel\"])")" ]]'

echo "[3] determinism across consecutive refresh calls"
out2="$("$IR_PROBE" --refresh)"
check "two consecutive refreshes match" '[[ "$out1" == "$out2" ]]'

echo "[4] --slug returns just the slug"
slug="$("$IR_PROBE" --slug)"
expected_slug="$(echo "$out1" | python3 -c "import json,sys; print(json.load(sys.stdin)['slug'])")"
check "--slug matches the JSON 'slug' field" '[[ "$slug" == "$expected_slug" ]]'

# Regression for the WSL2-on-fleet host case: when PATH carries a
# `lspci` entry that exists but cannot be exec'd (Windows-side stub or
# non-executable file), subprocess raises PermissionError — not
# FileNotFoundError — and the probe must still emit a valid JSON
# document with the GPU model fallback ("unknown"). Drive this
# deterministically by planting a non-executable `lspci` stub at the
# front of PATH so the probe's subprocess call hits PermissionError.
# See engine/tools/py/ir_hardware_probe.py::_run.
echo "[5] missing-helper resilience"
stub_dir="$(mktemp -d)"
touch "$stub_dir/lspci"  # non-executable, mimics the WSL2 PATH stub case
missing_out="$(PATH="$stub_dir:$PATH" "$IR_PROBE" --refresh)"
rm -rf "$stub_dir"
check "valid JSON when lspci is non-executable" \
    'python3 -c "import json,sys; json.loads(sys.argv[1])" "$missing_out"'
check "gpu.model falls back to a non-empty string" \
    '[[ -n "$(echo "$missing_out" | python3 -c "import json,sys; print(json.load(sys.stdin)[\"gpu\"][\"model\"])")" ]]'

# ir_ref_bench schema check — skip silently if the bench isn't built.
echo "[6] ir_ref_bench output schema"
IR_REF_BENCH="$(find "$REPO_ROOT/build" -maxdepth 6 -type f -name ir_ref_bench -perm -u=x 2>/dev/null | head -1 || true)"
if [[ -n "$IR_REF_BENCH" && -x "$IR_REF_BENCH" ]]; then
    bench_out="$("$IR_REF_BENCH" 100)"
    check "valid JSON" \
        'python3 -c "import json,sys; json.loads(sys.argv[1])" "$bench_out"'
    check "ms+iters+ops keys present" \
        'python3 -c "import json,sys; d=json.loads(sys.argv[1]); assert {\"ms\",\"iters\",\"ops\"}.issubset(d)" "$bench_out"'
    check "ms is positive" \
        'python3 -c "import json,sys; d=json.loads(sys.argv[1]); assert d[\"ms\"] > 0.0" "$bench_out"'
    check "CLI iters honored" \
        'python3 -c "import json,sys; d=json.loads(sys.argv[1]); assert d[\"iters\"] == 100" "$bench_out"'
else
    echo "  SKIP: ir_ref_bench not built (run 'ir-build --target ir_ref_bench' first)"
fi

echo
echo "calibration_test.sh: $pass passed, $fail failed"
exit "$fail"
