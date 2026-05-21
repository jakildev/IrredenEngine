#!/usr/bin/env bash
# run_math_bench.sh — run the IrredenMathBench Catch2 microbenchmark suite
# and write a JSON report to save_files/bench/<git-sha>[-<label>].json.
#
# Usage:
#   scripts/perf/run_math_bench.sh
#   scripts/perf/run_math_bench.sh --label before-refactor
#   scripts/perf/run_math_bench.sh --out /tmp/custom.json
#
# Output:
#   save_files/bench/<git-sha>[-<label>].json   (Catch2 JSON reporter format)
#
# The JSON schema is Catch2 v3's native benchmark reporter output.
# Use jq '."test-run"."test-cases"' to extract per-test-case data, or
# jq '."test-run"."test-cases"[].runs' for per-run timing details.

set -euo pipefail

_FLEET_COMMON_SH="$(cd "$(dirname "${BASH_SOURCE[0]}")/../fleet" && pwd)/fleet-common.sh"
if [[ -f "$_FLEET_COMMON_SH" ]]; then
    # shellcheck source=../fleet/fleet-common.sh
    source "$_FLEET_COMMON_SH"
else
    detect_engine_root() {
        git rev-parse --show-toplevel 2>/dev/null || echo "$HOME/src/IrredenEngine"
    }
fi

ENGINE_ROOT="$(detect_engine_root)"
LABEL=""
OUT_OVERRIDE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --label) LABEL="$2"; shift 2 ;;
        --out) OUT_OVERRIDE="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,22p' "$0"
            exit 0
            ;;
        *) echo "unknown arg: $1" >&2; exit 1 ;;
    esac
done

BUILD_DIR="$ENGINE_ROOT/build"
BENCH_EXE="$BUILD_DIR/IrredenMathBench"

if [[ ! -x "$BENCH_EXE" ]]; then
    echo "error: $BENCH_EXE not found — run fleet-build --target IrredenMathBench first" >&2
    exit 1
fi

SHA="$(git -C "$ENGINE_ROOT" rev-parse --short HEAD)"
BENCH_DIR="$ENGINE_ROOT/save_files/bench"
mkdir -p "$BENCH_DIR"

if [[ -n "$OUT_OVERRIDE" ]]; then
    OUT_FILE="$OUT_OVERRIDE"
elif [[ -n "$LABEL" ]]; then
    OUT_FILE="$BENCH_DIR/${SHA}-${LABEL}.json"
else
    OUT_FILE="$BENCH_DIR/${SHA}.json"
fi

echo "running IrredenMathBench → $OUT_FILE"
"$BENCH_EXE" "--benchmark-warmup-time=100" "--reporter=json::out=${OUT_FILE}"
echo "done: $OUT_FILE"
