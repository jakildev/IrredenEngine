#!/usr/bin/env bash
# Every test_*.sh suite in this directory must carry the executable bit.
# run_all.sh invokes suites through an explicit interpreter (`bash "$f"`),
# so a 100644 one still runs in CI — but it returns exit 126 under the
# direct `./"$f"` form its shebang implies, and any `[[ -x "$f" ]] || continue`
# filter a future runner adds would skip it silently. See #2725.
# lib_assert.sh (sourced, never run) and the test_*.py suites (always run as
# `python3 "$f"`, 100644 by convention) are excluded by the test_*.sh glob.

set -uo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
source "$SCRIPT_DIR/lib_assert.sh"

echo "T1: every test_*.sh suite is executable"
for f in "$SCRIPT_DIR"/test_*.sh; do
    [[ -f "$f" ]] || continue
    name=$(basename "$f")
    if [[ -x "$f" ]]; then
        ok "$name is executable"
    else
        bad "$name is committed non-executable (git update-index --chmod=+x $name)"
    fi
done

summarize "suite executable-bit guard"
