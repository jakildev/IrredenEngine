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
#
# Engine-root fallback convention: any fleet script that needs the engine
# root as a fallback must use `${FLEET_ENGINE_ROOT:-$HOME/src/IrredenEngine}`
# (as on the next line), never a bare `$HOME/src/IrredenEngine` — non-standard
# clone paths (e.g. the Windows fleet) override the location via
# FLEET_ENGINE_ROOT. Prefer sourcing this helper and calling
# detect_engine_root over hand-rolling the fallback.
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

# canonicalize_path_spelling — normalize a path to one spelling so the two ways
# the same directory is spelled on native Windows/MSYS2 compare equal in a plain
# string containment test. `git rev-parse --show-toplevel` yields a Windows drive
# path (C:/Users/x) while an MSYS2 shell's $PWD yields the POSIX drive form
# (/c/Users/x); byte-compared they differ, so a naive prefix check reports the
# scope as "outside" the engine root even when it is the same tree (#2036).
#
# Canonical form: /<lowercase-drive>/<rest> with forward slashes, e.g. both
# `C:/Users/x` and `/c/Users/x` map to `/c/Users/x`. On macOS/Linux every path
# is returned byte-unchanged: no POSIX absolute path begins with a `<letter>:`
# drive, and the single-letter-top-dir branch only rewrites an UPPERCASE drive
# letter (`/C/…` → `/c/…`), a spelling MSYS2's realpath never emits and a real
# POSIX mount effectively never uses — so the transforms are inert off-Windows.
# Kept bash-3.2 compatible (tr, not ${var,,}) for the /bin/bash and MSYS2 hosts.
canonicalize_path_spelling() {
    local p=$1
    p=${p//\\//} # backslashes -> forward slashes (C:\Users\x -> C:/Users/x)
    if [[ $p =~ ^([A-Za-z]):(/.*)?$ ]]; then
        # drive-letter form (C:/Users/x, c:) -> /c/Users/x, /c
        local drive rest
        drive=$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')
        rest=${BASH_REMATCH[2]}
        printf '/%s%s' "$drive" "$rest"
        return 0
    fi
    if [[ $p =~ ^/([A-Z])(/.*)?$ ]]; then
        # uppercase POSIX drive form (/C/Users/x) -> /c/Users/x
        local drive2 rest2
        drive2=$(printf '%s' "${BASH_REMATCH[1]}" | tr '[:upper:]' '[:lower:]')
        rest2=${BASH_REMATCH[2]}
        printf '/%s%s' "$drive2" "$rest2"
        return 0
    fi
    printf '%s' "$p"
}
