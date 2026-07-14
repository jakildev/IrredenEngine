#!/usr/bin/env bash
# fleet-net.sh — bound every network call an unattended fleet daemon makes, so a
# black-holed GitHub connection (silent TCP death, no RST) fails fast instead of
# hanging forever. (#2362)
#
# The problem this solves: the host's connections to GitHub intermittently
# black-hole. Any unguarded `git fetch` / `git push` / `gh …` then blocks on a
# network read that never returns — three fleet-wide outages in four days, each
# a single hung call: fleet-rebase's fetch holding the rebase lock for 3 days;
# the dispatcher main loop wedged 18 h on an inline `fleet-claim … gh issue
# view`; a fresh dispatcher hung at startup on its clone-freshness fetch.
#
# The fix is a pair of shadow functions — `git()` and `gh()` — that prefix a
# `timeout` to network operations. A script SOURCES this lib once and every
# subsequent `git`/`gh` call it makes (including calls inside functions it later
# sources, since bash resolves function names at call time) is guarded, with no
# per-call-site edits. fleet-claim alone has ~49 `gh` sites; a shadow guards
# them all — and every future site — by construction.
#
# Source of truth: scripts/fleet/fleet-net.sh in the engine repo.
# Installed to ~/bin/fleet-net.sh (as a symlink) by scripts/fleet/install.sh —
# it must be symlinked alongside the other fleet scripts so the FLEET_LIB_DIR
# source pattern resolves through ~/bin, the same way fleet-common.sh and
# fleet-clone-freshness.sh do.
#
# Usage (bash consumers — fleet-rebase / fleet-claim / fleet-dispatcher):
#   source "$FLEET_LIB_DIR/fleet-net.sh"     # FLEET_LIB_DIR is symlink-resolved
#   git -C "$wt" fetch origin master         # now bounded by FLEET_NET_TIMEOUT
#
# The scout is python and does NOT source this — python subprocesses invoke the
# `git`/`gh` executables directly (no bash function resolution), and its own
# fetchers (fleet_gh_poll.py) already carry urllib/subprocess timeouts.
#
# Env:
#   FLEET_TIMEOUT_CMD   — override the resolved timeout runner (e.g. a test
#                         double, or "" to force the unguarded passthrough).
#   FLEET_NET_TIMEOUT   — per-call budget in seconds (default 120).

# --- Resolve a coreutils-compatible `timeout` runner, once -------------------
# GNU coreutils ships it as `timeout` on Linux and `gtimeout` on macOS
# (Homebrew, non-conflicting name). A bare `command -v timeout` is NOT enough:
# Windows System32 ships an unrelated `timeout.exe` (an interactive wait, not a
# command runner) that can shadow coreutils in some PATH orders. Probe
# `--version` for the coreutils signature so the wrong binary is rejected; the
# ~/bin python fallback shim (install.sh step c) advertises "coreutils" in its
# own --version so it passes the same probe.
_fleet_net_is_coreutils_timeout() {
    command -v "$1" >/dev/null 2>&1 || return 1
    "$1" --version 2>/dev/null | grep -qi 'coreutils' || return 1
    return 0
}

_fleet_net_resolve_timeout_cmd() {
    local c
    for c in timeout gtimeout; do
        if _fleet_net_is_coreutils_timeout "$c"; then
            printf '%s' "$c"
            return 0
        fi
    done
    printf ''  # none found — callers degrade to unguarded passthrough
    return 0
}

# Respect an inherited value (a parent daemon may export it so children skip the
# re-probe); otherwise resolve it now. Empty is a valid resolved value meaning
# "no timeout binary on this host" — the shadows then pass straight through.
FLEET_TIMEOUT_CMD="${FLEET_TIMEOUT_CMD-$(_fleet_net_resolve_timeout_cmd)}"
FLEET_NET_TIMEOUT="${FLEET_NET_TIMEOUT:-120}"

# --- git() shadow: guard network subcommands only ----------------------------
# A false timeout on a LOCAL op (rebase, checkout) would corrupt the worktree
# mid-operation — the exact hazard the rebase lock exists to prevent — so the
# timeout is applied ONLY when the resolved subcommand is a network verb.
# Everything else passes through untouched. Misparsing a flag can at worst miss
# guarding a network op (degrades to today's unguarded behavior); it can never
# apply a timeout to a local op, because that requires the parsed subcommand to
# literally be a network verb.
git() {
    local tmo="${FLEET_TIMEOUT_CMD:-}"
    if [[ -z "$tmo" ]]; then
        command git "$@"
        return $?
    fi
    # Scan past git's global options to find the subcommand. The only global
    # flags that consume a SEPARATE following token are `-C <path>` and
    # `-c <name=value>`; every other global flag is either `--flag` or
    # `--flag=value` (self-contained). fleet's call sites use `git -C <wt> …`.
    local -a a=("$@")
    local i=0 n=${#a[@]} sub=""
    while (( i < n )); do
        case "${a[i]}" in
            -C|-c) i=$((i + 2)); continue ;;
            -*)    i=$((i + 1)); continue ;;
            *)     sub="${a[i]}"; break ;;
        esac
    done
    case "$sub" in
        fetch|push|pull|clone|ls-remote|remote|fetch-pack|send-pack)
            command "$tmo" "$FLEET_NET_TIMEOUT" git "$@"
            return $?
            ;;
        *)
            command git "$@"
            return $?
            ;;
    esac
}

# --- gh() shadow: every gh call is a network call ----------------------------
# No subcommand parsing needed — the GitHub CLI hits the API for essentially
# everything, so guard unconditionally.
gh() {
    local tmo="${FLEET_TIMEOUT_CMD:-}"
    if [[ -z "$tmo" ]]; then
        command gh "$@"
        return $?
    fi
    command "$tmo" "$FLEET_NET_TIMEOUT" gh "$@"
    return $?
}
