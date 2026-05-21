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
#   scripts/perf/perf_grid_matrix.sh --label baseline    # output dir suffix
#   scripts/perf/perf_grid_matrix.sh --frames 600        # frames per cell (default 300)
#   scripts/perf/perf_grid_matrix.sh --full              # extended matrix (30 cells)
#   scripts/perf/perf_grid_matrix.sh --quick             # smoke matrix (2 cells)
#   scripts/perf/perf_grid_matrix.sh --grid-size 32      # smaller grid (default 64)
#
# Output:
#   save_files/perf/<git-sha>[-<label>]/<cell-id>.txt    # raw profile_report.txt
#   save_files/perf/<git-sha>[-<label>]/manifest.json    # cell metadata + git state
#
# Cell ID format: target=<exe>,zoom=<z>,sub_mode=<m>,sub_base=<n>,grid=<g>
#
# Skills/agents: invoke this once before a change, once after, then
# pass both output dirs to compare_perf_runs.py.

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
TARGET="IRPerfGrid"
LABEL=""
FRAMES=300
GRID_SIZE=""
TIMEOUT=90
MATRIX="default"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target) TARGET="$2"; shift 2 ;;
        --label) LABEL="$2"; shift 2 ;;
        --frames) FRAMES="$2"; shift 2 ;;
        --grid-size) GRID_SIZE="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --full) MATRIX="full"; shift ;;
        --quick) MATRIX="quick"; shift ;;
        --default) MATRIX="default"; shift ;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *)
            echo "perf_grid_matrix.sh: unknown arg '$1' (try --help)" >&2
            exit 2
            ;;
    esac
done

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

CELL_COUNT=$(( ${#ZOOMS[@]} * ${#SUB_MODES[@]} * ${#SUB_BASES[@]} ))
echo "perf_grid_matrix: target=$TARGET matrix=$MATRIX cells=$CELL_COUNT frames=$FRAMES out=$OUT_DIR"

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
{
    echo "{"
    echo "  \"target\": \"$TARGET\","
    echo "  \"matrix\": \"$MATRIX\","
    echo "  \"frames\": $FRAMES,"
    echo "  \"git_sha\": \"$SHA\","
    echo "  \"git_dirty\": $([[ -n "$DIRTY" ]] && echo true || echo false),"
    echo "  \"run_name\": \"$RUN_NAME\","
    echo "  \"started_at\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\","
    echo "  \"cells\": ["
} > "$MANIFEST"

FIRST=1
INDEX=0
for ZOOM in "${ZOOMS[@]}"; do
    for SUB_MODE in "${SUB_MODES[@]}"; do
        for SUB_BASE in "${SUB_BASES[@]}"; do
            INDEX=$((INDEX + 1))
            CELL_ID="target=${TARGET},zoom=${ZOOM},sub_mode=${SUB_MODE},sub_base=${SUB_BASE}"
            if [[ -n "$GRID_SIZE" ]]; then
                CELL_ID="${CELL_ID},grid=${GRID_SIZE}"
            fi
            CELL_TXT="$OUT_DIR/${CELL_ID}.txt"
            CELL_LOG="$OUT_DIR/${CELL_ID}.log"
            echo "[$INDEX/$CELL_COUNT] $CELL_ID"

            CELL_ARGS=(--auto-profile "$FRAMES" --zoom "$ZOOM"
                       --subdivision-mode "$SUB_MODE"
                       --base-subdivisions "$SUB_BASE")
            if [[ -n "$GRID_SIZE" ]]; then
                CELL_ARGS+=(--grid-size "$GRID_SIZE")
            fi

            MARKER="$OUT_DIR/.cell_marker"
            touch "$MARKER"

            STATUS=0
            fleet-run --timeout "$TIMEOUT" "$TARGET" "${CELL_ARGS[@]}" \
                > "$CELL_LOG" 2>&1 || STATUS=$?

            REPORT="$(find_latest_report "$MARKER")"
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
    {"id": "$CELL_ID", "zoom": $ZOOM, "sub_mode": "$SUB_MODE", "sub_base": $SUB_BASE, "exit_status": $STATUS, "status": "$CELL_STATUS", "report": "${CELL_ID}.txt"}
EOF
        done
    done
done

{
    echo ""
    echo "  ],"
    echo "  \"finished_at\": \"$(date -u +%Y-%m-%dT%H:%M:%SZ)\""
    echo "}"
} >> "$MANIFEST"

echo "perf_grid_matrix: done, output in $OUT_DIR"
echo "  next: scripts/perf/perf_summary.py $OUT_DIR"
echo "  diff: scripts/perf/compare_perf_runs.py <baseline> $OUT_DIR"
