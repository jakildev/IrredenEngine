#!/usr/bin/env bash
# Tests for fleet-clip's before/after clip compose (H.264 MP4 + palette GIF),
# the shared attach-screenshots flow's video counterpart to the PNG pair.
#
# Drives fleet-clip against two synthetic `testsrc`/`testsrc2` inputs
# (ffmpeg -f lavfi) and checks the composed outputs through ffprobe rather
# than by hand — a hand-audited "looks right" pass over binary media is not
# reproducible. Also covers the missing-ffmpeg exit path (3, one line), which
# the shared flow's gate step depends on to skip the clip cleanly.
#
# Environment-dependency skip (ffmpeg/ffprobe absent) is exit 0; a missing
# fleet-clip subject is exit 3 with a SKIP: prefix, per run_all.sh's
# skip-status contract.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source "$(dirname "$0")/lib_preflight.sh"
FLEET_CLIP="$SCRIPT_DIR/fleet-clip"

if [[ ! -x "$FLEET_CLIP" ]]; then
    echo "SKIP: fleet-clip not found at $FLEET_CLIP" >&2
    exit 3
fi
if ! command -v ffmpeg >/dev/null 2>&1 || ! command -v ffprobe >/dev/null 2>&1; then
    echo "SKIP: ffmpeg/ffprobe not available on PATH" >&2
    exit 0
fi

source "$(dirname "$0")/lib_assert.sh"

WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

ffmpeg -y -loglevel error -f lavfi -i testsrc=size=320x180:rate=30 -t 2 "$WORKDIR/before.mp4"
ffmpeg -y -loglevel error -f lavfi -i testsrc2=size=320x180:rate=30 -t 2 "$WORKDIR/after.mp4"

# --- Happy path: compose, then inspect through ffprobe ----------------------
"$FLEET_CLIP" "$WORKDIR/before.mp4" "$WORKDIR/after.mp4" "$WORKDIR/out" \
    >"$WORKDIR/stdout.log" 2>"$WORKDIR/stderr.log"
assert_eq "$?" "0" "fleet-clip exits 0 on a valid before/after pair"

[[ -f "$WORKDIR/out.mp4" ]] && ok "out.mp4 was written" || bad "out.mp4 was written"
[[ -f "$WORKDIR/out.gif" ]] && ok "out.gif was written" || bad "out.gif was written"

MP4_WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$WORKDIR/out.mp4" 2>/dev/null)
MP4_HEIGHT=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$WORKDIR/out.mp4" 2>/dev/null)
assert_eq "${MP4_WIDTH:-}" "640" "out.mp4 width is 2x the 320px-wide input"
assert_eq "${MP4_HEIGHT:-}" "180" "out.mp4 height matches the input"

GIF_WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$WORKDIR/out.gif" 2>/dev/null)
if [[ -n "$GIF_WIDTH" && "$GIF_WIDTH" -le 640 ]]; then
    ok "out.gif width ($GIF_WIDTH) is <= 640px"
else
    bad "out.gif width (${GIF_WIDTH:-<empty>}) is <= 640px"
fi

GIF_FPS_RAW=$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of csv=p=0 "$WORKDIR/out.gif" 2>/dev/null)
GIF_FPS_OK=$(python3 -c "
n, d = '${GIF_FPS_RAW:-0/1}'.split('/')
print('yes' if (float(n) / float(d) if float(d) else 0.0) <= 15.0 else 'no')
" 2>/dev/null)
if [[ "$GIF_FPS_OK" == "yes" ]]; then
    ok "out.gif frame rate ($GIF_FPS_RAW) is <= 15fps"
else
    bad "out.gif frame rate (${GIF_FPS_RAW:-<empty>}) is <= 15fps"
fi

# --- A missing ffmpeg binary exits 3 with a one-line message ----------------
STDERR_NO_FFMPEG=$(PATH=/usr/bin:/bin "$FLEET_CLIP" "$WORKDIR/before.mp4" "$WORKDIR/after.mp4" "$WORKDIR/out-noffmpeg" 2>&1 >/dev/null)
RC_NO_FFMPEG=$?
assert_eq "$RC_NO_FFMPEG" "3" "fleet-clip exits 3 when ffmpeg is not on PATH"
NO_FFMPEG_LINES=$(printf '%s\n' "$STDERR_NO_FFMPEG" | grep -c '.')
assert_eq "$NO_FFMPEG_LINES" "1" "the missing-ffmpeg message is exactly one line"
[[ ! -f "$WORKDIR/out-noffmpeg.mp4" ]] && ok "no partial output on the missing-ffmpeg path" \
    || bad "no partial output on the missing-ffmpeg path"

# --- A missing input file exits non-zero and names the file ----------------
STDERR_MISSING_INPUT=$(PATH="$PATH" "$FLEET_CLIP" "$WORKDIR/nope.mp4" "$WORKDIR/after.mp4" "$WORKDIR/out-missing" 2>&1 >/dev/null)
RC_MISSING_INPUT=$?
[[ "$RC_MISSING_INPUT" -ne 0 ]] && ok "fleet-clip exits non-zero on a missing input" \
    || bad "fleet-clip exits non-zero on a missing input"
assert_contains "$STDERR_MISSING_INPUT" "nope.mp4" "the missing-input message names the missing file"

summarize "fleet-clip tests"
