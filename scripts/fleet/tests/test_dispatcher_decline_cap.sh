#!/usr/bin/env bash
# Keep the dispatcher's decline-cap contract in the `--only decline` slice.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=lib_preflight.sh
source "$SCRIPT_DIR/lib_preflight.sh"
# shellcheck source=lib_assert.sh
source "$SCRIPT_DIR/lib_assert.sh"

if bash "$SCRIPT_DIR/test_dispatcher_completion.sh"; then
    ok "dispatcher completion suite covers temporary-decline caps"
else
    bad "dispatcher completion suite failed"
fi

summarize "dispatcher decline-cap tests"
