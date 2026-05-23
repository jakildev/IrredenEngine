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

echo
echo "calibration_test.sh: $pass passed, $fail failed"
exit "$fail"
