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

# --- install-symlink freshness (#2262) --------------------------------------
# New fleet scripts / role slash-commands / ir-* tools merge to master, but a
# host's ~/bin symlinks are only (re)created by install.sh — so a tool added
# mid-run is `command not found` on first use (fleet-review-verdict stranded
# every reviewer on 2026-07-04). The scout keeps the main clone fresh via
# `git pull`, which bumps a file's mtime ONLY when it changes/adds it, so
# "any install source newer than the last install.sh pass" is exactly "a
# tool ~/bin has not linked yet". fleet-up (bring-up) and fleet-dispatch-wrap
# (per pane) call fleet_install_maybe_refresh before launching a claude.
#
# Overridable for tests: FLEET_INSTALL_STAMP, FLEET_INSTALL_LOCK.

# fleet_main_clone_root [root] — resolve the MAIN clone from a repo root that
# may be a linked worktree (a worktree's `.git` is a file, not a dir). Mirrors
# install.sh's worktree->main-clone resolution so the stale-check compares the
# symlink target the scout pulls into, never an ephemeral/reset worktree tree.
fleet_main_clone_root() {
    local root="${1:-}"
    [[ -n "$root" ]] || root="$(detect_engine_root)"
    if [[ -f "$root/.git" ]]; then
        local main
        main="$(git -C "$root" worktree list --porcelain 2>/dev/null \
            | awk '/^worktree /{print $2; exit}')"
        [[ -n "$main" && -d "$main" ]] && root="$main"
    fi
    printf '%s' "$root"
}

# fleet_install_stale [main-clone-root] — exit 0 (stale) when the stamp is
# missing or any symlinked source is newer than it; exit 1 (fresh) otherwise.
# `find -print -quit` short-circuits on the first newer file, so the fresh
# common path is a handful of sub-ms stat walks. `-type f` skips the dirs
# themselves (e.g. scripts/fleet/__pycache__, whose mtime bumps on every .pyc
# write and would false-positive). Name globs mirror what install.sh links:
# every top-level scripts/fleet file, ir-* under engine/tools/bin, role-*.md
# under the engine + game commands dirs.
fleet_install_stale() {
    local root="${1:-$(fleet_main_clone_root)}"
    local stamp="${FLEET_INSTALL_STAMP:-$HOME/.fleet/state/.install-stamp}"
    [[ -f "$stamp" ]] || return 0   # never installed / stamp cleared -> stale
    local hit dir
    if [[ -d "$root/scripts/fleet" ]]; then
        hit=$(find "$root/scripts/fleet" -maxdepth 1 -type f -newer "$stamp" -print -quit 2>/dev/null)
        if [[ -n "$hit" ]]; then return 0; fi
    fi
    if [[ -d "$root/engine/tools/bin" ]]; then
        hit=$(find "$root/engine/tools/bin" -maxdepth 1 -type f -name 'ir-*' -newer "$stamp" -print -quit 2>/dev/null)
        if [[ -n "$hit" ]]; then return 0; fi
    fi
    for dir in "$root/.claude/commands" "$root/creations/game/.claude/commands"; do
        [[ -d "$dir" ]] || continue
        hit=$(find "$dir" -maxdepth 1 -type f -name 'role-*.md' -newer "$stamp" -print -quit 2>/dev/null)
        if [[ -n "$hit" ]]; then return 0; fi
    done
    return 1
}

# fleet_install_refresh [main-clone-root] — run install.sh --no-zshrc to re-link
# ~/bin, then bump the stamp. The stamp is bumped REGARDLESS of install.sh's
# exit code (install.sh also stamps on success): a host that CANNOT symlink
# (the Windows native-symlink hard-exit) still advances the stamp, so it
# degrades to one warning per new-tool-merge instead of one per dispatch.
# Callers guard with fleet_install_stale, so this only fires on a real change.
fleet_install_refresh() {
    local root="${1:-$(fleet_main_clone_root)}"
    local stamp="${FLEET_INSTALL_STAMP:-$HOME/.fleet/state/.install-stamp}"
    local installer="$root/scripts/fleet/install.sh"
    if [[ -x "$installer" ]]; then
        # --no-zshrc: an automated refresh must never edit the user's ~/.zshrc.
        IRREDEN_INSTALL_SKIP_ZSHRC=1 "$installer" --no-zshrc \
            || echo "fleet: install.sh auto-refresh exited non-zero (see above)" >&2
    fi
    mkdir -p "$(dirname "$stamp")" 2>/dev/null || true
    touch "$stamp" 2>/dev/null || true
}

# fleet_install_maybe_refresh [main-clone-root] — the guarded entry point both
# hooks call. Stale-check first (sub-ms common path); if stale, take an atomic
# mkdir-lock (same primitive fleet-claim uses; NOT flock, absent on macOS) so
# concurrent pane dispatches don't race install.sh's `ln -sf` + stamp, re-check
# under the lock (double-checked locking — a peer may have just refreshed), and
# refresh. Never blocks dispatch: a crashed lock holder is stolen after 1 min,
# and a live holder is waited on only briefly before proceeding anyway.
fleet_install_maybe_refresh() {
    local root="${1:-$(fleet_main_clone_root)}"
    fleet_install_stale "$root" || return 0   # fresh -> zero work

    local lock="${FLEET_INSTALL_LOCK:-$HOME/.fleet/state/.install-refresh.lock}"
    local wait_secs="${FLEET_INSTALL_WAIT_SECS:-1}"
    local wait_max="${FLEET_INSTALL_WAIT_MAX:-12}"
    mkdir -p "$(dirname "$lock")" 2>/dev/null || true
    local waited=0
    while ! mkdir "$lock" 2>/dev/null; do
        # Steal a lock a crashed holder left behind (install.sh finishes in a
        # couple seconds, so >1 min old means the holder died).
        if [[ -n "$(find "$lock" -maxdepth 0 -mmin +1 2>/dev/null)" ]]; then
            rmdir "$lock" 2>/dev/null || true
            continue
        fi
        # A live peer holds it. If its refresh already made us fresh, we're done.
        fleet_install_stale "$root" || return 0
        sleep "$wait_secs"
        waited=$((waited + 1))
        if (( waited >= wait_max )); then return 0; fi   # cap -> proceed, don't wedge dispatch
    done
    if fleet_install_stale "$root"; then
        fleet_install_refresh "$root"
    fi
    rmdir "$lock" 2>/dev/null || true
}
