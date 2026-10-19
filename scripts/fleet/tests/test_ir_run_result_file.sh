#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/lib_preflight.sh"
source "$SCRIPT_DIR/lib_assert.sh"

ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/ir-run-result.XXXXXX")"
trap 'rm -rf "$tmp"' EXIT
build="$tmp/build"
mkdir -p "$build/bin"

make_stub() {
    local name="$1"
    local body="$2"
    {
        echo '#!/usr/bin/env bash'
        echo "$body"
    } > "$build/bin/$name"
    chmod +x "$build/bin/$name"
}

run_case() {
    local expected="$1"
    local name="$2"
    shift 2
    local result="$tmp/$name.json"
    local output="$tmp/$name.log"
    set +e
    "$ROOT/engine/tools/bin/ir-run" --build-dir "$build" --result-file "$result" "$@" \
        > "$output" 2>&1
    local rc=$?
    set -e
    assert_eq "$expected" "$(jq -r .verdict "$result")" "$name JSON verdict"
    assert_contains "$(<"$output")" "RESULT=$expected" "$name stdout verdict"
    assert_eq "$name" "$(jq -r .exe "$result")" "$name executable"
    assert_eq "$rc" "$(jq -r .exit "$result")" "$name exit code"
    jq -e '.signal == null or (.signal | type == "string")' "$result" >/dev/null || \
        bad "$name signal schema"
    jq -e '.argv | type == "array"' "$result" >/dev/null || bad "$name argv schema"
    jq -e '.at | type == "string" and length > 0' "$result" >/dev/null || \
        bad "$name timestamp schema"
    [[ $rc -ge 0 ]] || bad "$name returned an invalid status"
}

make_stub CleanStub 'exit 0'
run_case CLEAN CleanStub CleanStub $'control-\001-\b-\f-end'

make_stub CrashStub 'exit 7'
run_case CRASH CrashStub CrashStub

make_stub TimeoutStub 'sleep 10'
run_case ALIVE-TIMEOUT TimeoutStub --timeout 1 TimeoutStub

result_env="$tmp/env.json"
IR_RUN_RESULT_FILE="$result_env" "$ROOT/engine/tools/bin/ir-run" \
    --build-dir "$build" CleanStub >/dev/null
assert_eq CLEAN "$(jq -r .verdict "$result_env")" "environment fallback"

summarize
