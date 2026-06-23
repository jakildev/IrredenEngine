#!/usr/bin/env bash
# fleet-common.sh — shared helpers sourced by fleet-build, fleet-run, and fleet-run-targets.
#
# Source of truth: scripts/fleet/fleet-common.sh in the engine repo.
# Installed to ~/bin/fleet-common.sh (as a symlink) by scripts/fleet/install.sh.
#
# Usage:
#   _FLEET_COMMON_SH="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fleet-common.sh"
#   source "$_FLEET_COMMON_SH"
#
# Works whether the calling script is invoked directly from the repo or
# via a ~/bin symlink, because all fleet scripts are symlinked into the
# same directory and fleet-common.sh is also symlinked there.

# detect_engine_root — walk up from the git root to find the directory
# that contains CMakePresets.json (handles linked worktrees where the
# git root is a nested .claude/worktrees/… path).
detect_engine_root() {
    local root
    root=$(git rev-parse --show-toplevel 2>/dev/null || echo "${FLEET_ENGINE_ROOT:-$HOME/src/IrredenEngine}")
    if [[ ! -f "$root/CMakePresets.json" ]]; then
        if [[ -f "$root/../../CMakePresets.json" ]]; then
            root="$(cd "$root/../.." && pwd)"
        elif [[ -f "$root/../CMakePresets.json" ]]; then
            root="$(cd "$root/.." && pwd)"
        fi
    fi
    echo "$root"
}
