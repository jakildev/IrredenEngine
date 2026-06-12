#!/usr/bin/env bash
# perf_grid_matrix.sh — drive IRPerfGrid (or IRLuaPerfGrid) across a
# matrix of (zoom × subdivision_mode × base_subdivisions) cells and
# collect one save_files/profile_report.txt per cell into a single
# output directory. Output is consumed by scripts/perf/compare_perf_runs.py
# and scripts/perf/perf_summary.py.
#
# Usage:
#   scripts/perf/perf_grid_matrix.sh                     # default 12-cell matrix, IRPerfGrid
#   scripts/perf/perf_grid_matrix.sh --target IRLuaPerfGrid
#   scripts/perf/perf_grid_matrix.sh --target both       # IRPerfGrid + IRLuaPerfGrid (parity run)
#   scripts/perf/perf_grid_matrix.sh --label baseline    # output dir suffix
#   scripts/perf/perf_grid_matrix.sh --frames 600        # frames per cell (default 300)
#   scripts/perf/perf_grid_matrix.sh --full              # extended matrix (30 cells)
#   scripts/perf/perf_grid_matrix.sh --quick             # smoke matrix (2 cells)
#   scripts/perf/perf_grid_matrix.sh --grid-size 32      # smaller grid (default 64)
#   scripts/perf/perf_grid_matrix.sh --presets <dir>     # sweep *.lua preset files
#   scripts/perf/perf_grid_matrix.sh --threading-baseline
#       # 9-cell threading baseline: {4K,32K,262K} entities × {0,1,hw-2} worker_threads.
#       # Use this before T-221 lands to capture the serial-execution floor so
#       # the after-threading run shows the real speedup on the UPDATE pipeline.
#
# Output:
#   save_files/perf/<git-sha>[-<label>]/<cell-id>.txt    # raw profile_report.txt
#   save_files/perf/<git-sha>[-<label>]/manifest.json    # cell metadata + git state
#
# Cell ID format (matrix mode):    target=<exe>,zoom=<z>,sub_mode=<m>,sub_base=<n>[,grid=<g>]
# Cell ID format (preset mode):    target=<exe>,preset=<filename-without-ext>
# Cell ID format (threading mode): target=<exe>,grid=<g>,worker_threads=<n>
#
# Skills/agents: for before/after comparisons invoke once per tree and diff
# with compare_perf_runs.py. For Lua-vs-C++ parity, use --target both and
# feed the output dir to scripts/perf/lua_cpp_parity.py.

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

# sysctl on macOS, nproc on Linux/Windows-MSYS2.
hw_concurrency() {
    if command -v nproc >/dev/null 2>&1; then
        nproc
    else
        sysctl -n hw.ncpu 2>/dev/null || echo 4
    fi
}

ENGINE_ROOT="$(detect_engine_root)"
TARGET="IRPerfGrid"
TARGET_BOTH=false
LABEL=""
FRAMES=300
GRID_SIZE=""
TIMEOUT=90
MATRIX="default"
PRESETS_DIR=""
THREADING_BASELINE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            if [[ "$2" == "both" ]]; then
                TARGET_BOTH=true
            else
                TARGET="$2"
            fi
            shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --grid-size) GRID_SIZE="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --full) MATRIX="full"; shift ;;
        --quick) MATRIX="quick"; shift ;;
        --default) MATRIX="default"; shift ;;
        --presets) PRESETS_DIR="$2"; shift 2 ;;
        --threading-baseline) THREADING_BASELINE=true; shift ;;
        -h|--help)
            sed -n '2,35p' "$0"
            exit 0
            ;;
        *)
            echo "perf_grid_matrix.sh: unknown arg '$1' (try --help)" >&2
            exit 2
            ;;
    esac
done

if $TARGET_BOTH; then
    TARGETS=("IRPerfGrid" "IRLuaPerfGrid")
else
    TARGETS=("$TARGET")
fi

if $THREADING_BASELINE; then
    # 9-cell baseline: 3 grid sizes × 3 worker_thread values.
    # worker_threads is stored for cell-ID differentiation; the exe stubs it
    # until T-221 wires enkiTS. Numbers should be near-identical across the
    # worker_threads axis — that's the serial-execution floor for T-221 to beat.
    HW_N=$(hw_concurrency)
    HW_MINUS_2=$(( HW_N > 2 ? HW_N - 2 : 1 ))
    THREADING_GRID_SIZES=(16 32 64)
    THREADING_WORKER_THREADS=(0 1 "$HW_MINUS_2")
    CELL_COUNT=$(( ${#THREADING_GRID_SIZES[@]} * ${#THREADING_WORKER_THREADS[@]} * ${#TARGETS[@]} ))
    if [[ -z "$LABEL" ]]; then LABEL="threading-baseline"; fi
elif [[ -n "$PRESETS_DIR" ]]; then
    # Resolve relative PRESETS_DIR relative to ENGINE_ROOT so preset absolute
    # paths are valid when passed through fleet-run to the demo's cwd.
    if [[ "$PRESETS_DIR" != /* ]]; then
        PRESETS_DIR="$ENGINE_ROOT/$PRESETS_DIR"
    fi
    mapfile -t PRESET_FILES < <(find "$PRESETS_DIR" -maxdepth 1 -name "*.lua" | sort)
    CELL_COUNT=$(( ${#PRESET_FILES[@]} * ${#TARGETS[@]} ))
else
    case "$MATRIX" in
        quick)
            ZOOMS=(1 4)
            SUB_MODES=(full)
            SUB_BASES=(1)
            ;;
        default)
            ZOOMS=(1 2 4 8)
            SUB_MODES=(full position_only none)
            SUB_BASES=(1)
            ;;
        full)
            ZOOMS=(1 2 4 8 16)
            SUB_MODES=(full position_only none)
            SUB_BASES=(1 4)
            ;;
    esac
    CELL_COUNT=$(( ${#ZOOMS[@]} * ${#SUB_MODES[@]} * ${#SUB_BASES[@]} * ${#TARGETS[@]} ))
fi

TARGET_LABEL="$($TARGET_BOTH && echo "both" || echo "$TARGET")"

SHA="$(git -C "$ENGINE_ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)"
DIRTY=""
if ! git -C "$ENGINE_ROOT" diff --quiet 2>/dev/null; then
    DIRTY="-dirty"
fi
RUN_NAME="${SHA}${DIRTY}"
if [[ -n "$LABEL" ]]; then
    RUN_NAME="${RUN_NAME}-${LABEL}"
fi
OUT_DIR="$ENGINE_ROOT/save_files/perf/$RUN_NAME"
mkdir -p "$OUT_DIR"

if $THREADING_BASELINE; then
    echo "perf_grid_matrix: target=$TARGET_LABEL threading-baseline cells=$CELL_COUNT frames=$FRAMES hw=$HW_N out=$OUT_DIR"
elif [[ -n "$PRESETS_DIR" ]]; then
    echo "perf_grid_matrix: target=$TARGET_LABEL presets=$PRESETS_DIR cells=$CELL_COUNT frames=$FRAMES out=$OUT_DIR"
else
    echo "perf_grid_matrix: target=$TARGET_LABEL matrix=$MATRIX cells=$CELL_COUNT frames=$FRAMES out=$OUT_DIR"
fi

if ! command -v fleet-run >/dev/null 2>&1; then
    echo "perf_grid_matrix: fleet-run not on PATH; aborting" >&2
    exit 1
fi

# The demo runs from its own build dir (via fleet-run), so the profile
# report lands under build/creations/demos/<demo>/save_files/. Snapshot
# mtime before each cell and copy the freshest profile_report.txt
# afterwards — robust to whatever build dir layout fleet-run picks.
BUILD_ROOT="$ENGINE_ROOT/build"
find_latest_report() {
    find "$BUILD_ROOT" -path "*/save_files/profile_report.txt" \
        -newer "$1" -print 2>/dev/null | head -1
}

# manifest.json header — we append cell entries inside the loop.
MANIFEST="$OUT_DIR/manifest.json"
_MATRIX_KEY="$($THREADING_BASELINE && echo "threading_baseline" || echo "$MATRIX")"
{
    echo "{"
    echo "  \"target\": \"$TARGET_LABEL\","
    echo "  \"matrix\": \"$_MATRIX_KEY\","
    echo "  \"frames\": $FRAMES,"
    echo "  \"git_sha\": \"$SHA\","
    echo "  \"git_dirty\": $([[ -n "$DIRTY" ]] && echo true || echo false),"
    echo "  \"run_name\": \"$RUN_NAME\","
    echo "  \"started_at\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"cells\": ["
} > "$MANIFEST"

run_cell() {
    local RUN_TARGET="$1"
    local CELL_ID="$2"
    local CELL_META="$3"
    shift 3
    local CELL_ARGS=("$@")

    local CELL_TXT="$OUT_DIR/${CELL_ID}.txt"
    local CELL_LOG="$OUT_DIR/${CELL_ID}.log"
    echo "[$INDEX/$CELL_COUNT] $CELL_ID"

    local MARKER="$OUT_DIR/.cell_marker"
    touch "$MARKER"

    local STATUS=0
    fleet-run --timeout "$TIMEOUT" "$RUN_TARGET" "${CELL_ARGS[@]}" \
        > "$CELL_LOG" 2>&1 || STATUS=$?

    local REPORT
    REPORT="$(find_latest_report "$MARKER")"
    local CELL_STATUS
    if [[ -n "$REPORT" && -f "$REPORT" ]]; then
        cp "$REPORT" "$CELL_TXT"
        CELL_STATUS="ok"
    else
        CELL_STATUS="no_report"
        echo "  (no profile_report.txt produced; see $CELL_LOG)"
    fi

    if [[ $FIRST -eq 1 ]]; then
        FIRST=0
    else
        echo "," >> "$MANIFEST"
    fi
    cat >> "$MANIFEST" <<EOF
    {"id": "$CELL_ID", "target": "$RUN_TARGET", $CELL_META, "exit_status": $STATUS, "status": "$CELL_STATUS", "report": "${CELL_ID}.txt"}
EOF
}

FIRST=1
INDEX=0

if $THREADING_BASELINE; then
    for RUN_TARGET in "${TARGETS[@]}"; do
        for TG_SIZE in "${THREADING_GRID_SIZES[@]}"; do
            for TW in "${THREADING_WORKER_THREADS[@]}"; do
                INDEX=$((INDEX + 1))
                CELL_ID="target=${RUN_TARGET},grid=${TG_SIZE},worker_threads=${TW}"
                CELL_ARGS=(--auto-profile "$FRAMES"
                           --grid-size "$TG_SIZE"
                           --zoom 1
                           --subdivision-mode full
                           --base-subdivisions 1
                           --worker-threads "$TW")
                run_cell "$RUN_TARGET" "$CELL_ID" \
                    "\"grid\": $TG_SIZE, \"worker_threads\": $TW" \
                    "${CELL_ARGS[@]}"
            done
        done
    done
elif [[ -n "$PRESETS_DIR" ]]; then
    for RUN_TARGET in "${TARGETS[@]}"; do
        for PRESET in "${PRESET_FILES[@]}"; do
            INDEX=$((INDEX + 1))
            PRESET_NAME="$(basename "$PRESET" .lua)"
            CELL_ID="target=${RUN_TARGET},preset=${PRESET_NAME}"
            CELL_ARGS=(--auto-profile "$FRAMES" --config-preset "$PRESET")
            if [[ -n "$GRID_SIZE" ]]; then
                CELL_ARGS+=(--grid-size "$GRID_SIZE")
            fi
            run_cell "$RUN_TARGET" "$CELL_ID" "\"preset\": \"$PRESET_NAME\"" "${CELL_ARGS[@]}"
        done
    done
else
    for RUN_TARGET in "${TARGETS[@]}"; do
        for ZOOM in "${ZOOMS[@]}"; do
            for SUB_MODE in "${SUB_MODES[@]}"; do
                for SUB_BASE in "${SUB_BASES[@]}"; do
                    INDEX=$((INDEX + 1))
                    CELL_ID="target=${RUN_TARGET},zoom=${ZOOM},sub_mode=${SUB_MODE},sub_base=${SUB_BASE}"
                    if [[ -n "$GRID_SIZE" ]]; then
                        CELL_ID="${CELL_ID},grid=${GRID_SIZE}"
                    fi
                    CELL_ARGS=(--auto-profile "$FRAMES" --zoom "$ZOOM"
                               --subdivision-mode "$SUB_MODE"
                               --base-subdivisions "$SUB_BASE")
                    if [[ -n "$GRID_SIZE" ]]; then
                        CELL_ARGS+=(--grid-size "$GRID_SIZE")
                    fi
                    run_cell "$RUN_TARGET" "$CELL_ID" \
                        "\"zoom\": $ZOOM, \"sub_mode\": \"$SUB_MODE\", \"sub_base\": $SUB_BASE" \
                        "${CELL_ARGS[@]}"
                done
            done
        done
    done
fi

{
    echo ""
    echo "  ],"
    echo "  \"finished_at\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\""
    echo "}"
} >> "$MANIFEST"

echo "perf_grid_matrix: done, output in $OUT_DIR"
echo "  next: scripts/perf/perf_summary.py $OUT_DIR"
echo "  diff: scripts/perf/compare_perf_runs.py <baseline> $OUT_DIR"
if $TARGET_BOTH; then
    echo "  parity: scripts/perf/lua_cpp_parity.py $OUT_DIR"
fi
