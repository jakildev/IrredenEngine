#!/usr/bin/env bash
# fleet-clone-freshness.sh — keep the main clone's checked-out master current so
# scout / ingest / fleet-claim run the *current* fleet-script code, not a stale
# working tree. (#1810)
#
# The problem this solves: every fleet script is invoked through a ~/bin symlink
# that resolves to the main clone's working tree (~/src/IrredenEngine/scripts/
# fleet/). scout and fleet-claim also import their python parsers from that same
# tree (FLEET_LIB_DIR). fleet-up fetches + resets the *worktrees* to origin/master
# but NEVER fast-forwards the main clone's own checked-out master — so once a
# fleet-script fix merges (e.g. the #1783 blocked_by parser), the merged code
# stays inert until someone manually pulls. Issue *bodies* are read live via gh,
# so the data is fresh; only the parser/script CODE is stale. The fix advances
# the code.
#
# Source of truth: scripts/fleet/fleet-clone-freshness.sh in the engine repo.
# Installed to ~/bin/fleet-clone-freshness.sh (as a symlink) by
# scripts/fleet/install.sh — it must be symlinked alongside the other fleet
# scripts so the by-dir source pattern resolves through the ~/bin symlink, the
# same way fleet-common.sh does.
#
# Usage (bash consumers — fleet-up / fleet-dispatcher / fleet-claim):
#   source "$FLEET_LIB_DIR/fleet-clone-freshness.sh"          # FLEET_LIB_DIR is symlink-resolved
#   # or, where only the ~/bin sibling is known (fleet-up):
#   source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/fleet-clone-freshness.sh"
#
# The scout is python and deliberately does NOT source this — it computes the
# same freshness with a tiny inline `git rev-parse` (no new imported module; the
# scout's module resolution is fragile, #1750/#1578).
#
# Entry points:
#   clone_behind_count <repo_root>  — echo how many commits master is behind
#                                     origin/master (0 if equal/ahead/unknown).
#                                     rev-parse only, NEVER fetches.
#   assert_clone_fresh  <repo_root>  — return 1 + loud stderr if behind; else 0.
#                                     rev-parse only, NEVER fetches.
#   advance_main_clone  <repo_root>  — guarded, rate-limited fetch + ff-only
#                                     advance of a clean on-master clone. Never
#                                     switches branches, never resets --hard.
#   restore_main_clone_to_master <repo_root>
#                                   — fleet-up-time stronger variant: returns a
#                                     clone parked off-master (PR branch from a
#                                     cursor session, reviewer scratch leftover,
#                                     detached HEAD) to master when no tracked
#                                     WIP is at risk, then ff-advances. Never
#                                     touches a tree with tracked modifications.
#
# Env:
#   FLEET_SKIP_CLONE_FRESHNESS=1  — disable the assert_clone_fresh claim gate
#                                   (the claim test harness sets this; also an
#                                   operator escape hatch).
#   FLEET_STATE_DIR               — where the per-repo rate-limit sentinel lives
#                                   (defaults to ~/.fleet/state).

# clone_behind_count <repo_root>
# How many commits the clone's local master is behind its tracked
# origin/master. rev-parse / rev-list only — no fetch. 0 on any ambiguity
# (missing refs, diverged, ahead) so callers treat "unknown" as "fresh".
clone_behind_count() {
    local root="$1"
    [[ -d "$root/.git" ]] || { echo 0; return 0; }
    local local_head origin_head
    local_head="$(git -C "$root" rev-parse --verify --quiet refs/heads/master 2>/dev/null || true)"
    origin_head="$(git -C "$root" rev-parse --verify --quiet refs/remotes/origin/master 2>/dev/null || true)"
    if [[ -z "$local_head" || -z "$origin_head" || "$local_head" == "$origin_head" ]]; then
        echo 0
        return 0
    fi
    local behind
    behind="$(git -C "$root" rev-list --count master..origin/master 2>/dev/null || echo 0)"
    echo "${behind:-0}"
}

# assert_clone_fresh <repo_root>
# Fail-loud freshness gate for the claim path. rev-parse only (no fetch — relies
# on the origin/master ref that fleet-up / the dispatcher already fetched). When
# the clone is behind, refuse with a precise remedy so a stale parser can't
# silently false-grant a claim whose blocker it failed to see.
assert_clone_fresh() {
    local root="$1"
    # Opt-out: FLEET_SKIP_CLONE_FRESHNESS=1 disables the gate. Used by the claim
    # test harness (which points at the real, possibly-stale main clone but does
    # not care about its freshness) and available as an operator escape hatch.
    if [[ -n "${FLEET_SKIP_CLONE_FRESHNESS:-}" && "${FLEET_SKIP_CLONE_FRESHNESS}" != "0" ]]; then
        return 0
    fi
    local behind
    behind="$(clone_behind_count "$root")"
    if [[ "${behind:-0}" -gt 0 ]]; then
        echo "fleet-claim: main clone ($root) is $behind commit(s) behind origin/master —" >&2
        echo "             its fleet scripts/parsers are stale, so a blocker could be" >&2
        echo "             missed (silent false-grant). Refusing the claim. Advance with:" >&2
        echo "               git -C $root merge --ff-only origin/master   (or rerun fleet-up)" >&2
        return 1
    fi
    return 0
}

# advance_main_clone <repo_root>
# Guarded, rate-limited fast-forward of the main clone's master. Safe on the
# shared main checkout: advances ONLY when the clone is on branch master, has a
# clean working tree, and master is a strict pure-ancestor of origin/master
# (i.e. a real fast-forward). Any guard miss → skip + warn, never mutate. Mirrors
# fleet-up's reset_worktree dirty-guard idiom. Always returns 0 so a `set -e`
# caller is never aborted by a skipped advance.
advance_main_clone() {
    local root="$1"
    [[ -d "$root/.git" ]] || return 0

    # Rate-limit: at most one fetch per repo per 60s, so the dispatcher loop can
    # call this every tick without hammering the remote.
    local repo_tag sentinel now last
    repo_tag="$(basename "$root")"
    sentinel="${FLEET_STATE_DIR:-$HOME/.fleet/state}/.${repo_tag}-clone-advanced"
    mkdir -p "$(dirname "$sentinel")" 2>/dev/null || true
    now="$(date +%s)"
    if [[ -f "$sentinel" ]]; then
        last="$(cat "$sentinel" 2>/dev/null || echo 0)"
        if [[ "${last:-0}" =~ ^[0-9]+$ ]] && (( now < last + 60 )); then
            return 0
        fi
    fi
    git -C "$root" fetch origin master --quiet 2>/dev/null || true
    echo "$now" > "$sentinel" 2>/dev/null || true

    # Guard 1: must be on branch master (never touch a checked-out feature
    # branch — agents occasionally check one out in the shared main clone).
    local branch
    branch="$(git -C "$root" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
    if [[ "$branch" != "master" ]]; then
        echo "fleet-clone-freshness: $root is on '$branch' (not master) — skipping advance." >&2
        return 0
    fi
    # Guard 2: clean working tree (never clobber uncommitted work).
    local dirty
    dirty="$(git -C "$root" status --porcelain 2>/dev/null || echo SKIP)"
    if [[ -n "$dirty" && "$dirty" != "SKIP" ]]; then
        echo "fleet-clone-freshness: $root has uncommitted changes — skipping advance." >&2
        return 0
    fi
    # Guard 3 + the ff itself live in the shared tail.
    _ff_advance_to_origin_master "$root"
    return 0
}

# _ff_advance_to_origin_master <repo_root>
# Shared tail of advance_main_clone / restore_main_clone_to_master: guarded
# ff-only advance of an on-master clone whose origin/master ref is current
# (callers own the fetch). Diverged or up-to-date → no-op. Always returns 0.
_ff_advance_to_origin_master() {
    local root="$1"
    if ! git -C "$root" merge-base --is-ancestor master origin/master 2>/dev/null; then
        echo "fleet-clone-freshness: $root master has diverged from origin/master — skipping advance (human fixup needed)." >&2
        return 0
    fi
    local behind
    behind="$(git -C "$root" rev-list --count master..origin/master 2>/dev/null || echo 0)"
    if [[ "${behind:-0}" -le 0 ]]; then
        return 0  # already current (or ahead — left to the human)
    fi
    # ff-only refuses on its own rather than clobber a tracked modification
    # that overlaps the incoming commits; a disjoint dirty tree advances fine.
    if git -C "$root" merge --ff-only origin/master --quiet 2>/dev/null; then
        echo "fleet-clone-freshness: advanced $root master by $behind commit(s) to origin/master." >&2
    else
        echo "fleet-clone-freshness: ff-only advance of $root refused (overlapping local changes or concurrent git op) — leaving as-is." >&2
    fi
    return 0
}

# restore_main_clone_to_master <repo_root>
# fleet-up-time restore: get the shared main clone back onto an up-to-date
# master before the fleet starts. A clone parked off-master (a cursor session's
# PR branch, a stranded reviewer scratch branch, a detached HEAD) freezes the
# local master ref while origin advances, and assert_clone_fresh then refuses
# every claim — a silent whole-fleet stall (2026-07-13). advance_main_clone
# deliberately never switches branches (it runs unattended every dispatcher
# tick); this variant runs at the one moment branch-switching is safe to want,
# with WIP protection:
#   - tracked modifications anywhere (staged or not) → never touch, warn loudly.
#     Untracked files don't block: `git checkout` refuses on its own if one
#     would be overwritten, and stray junk files (0-byte `=`, .review-body.md)
#     are exactly what used to wedge the old flow.
#   - off-master + clean → checkout master.
#   - then ff-advance master to origin/master (fetch is the caller's job at
#     fleet-up; a cheap refresh here keeps standalone use correct).
# Always returns 0 — a skipped restore must not abort a `set -e` fleet-up; the
# warning + the claim gate's own refusal are the signal.
restore_main_clone_to_master() {
    local root="$1"
    [[ -d "$root/.git" ]] || return 0

    local tracked_dirty
    tracked_dirty="$(git -C "$root" status --porcelain 2>/dev/null | grep -v '^??' || true)"

    local branch
    branch="$(git -C "$root" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"

    # Tracked WIP wins over the restore on EITHER path (parked branch or already
    # on master). This must gate before the ff-advance below: _ff_advance's
    # ff-only refuses only when the incoming commits *overlap* the dirty files —
    # a disjoint dirty tree on master would otherwise be advanced silently
    # underneath uncommitted work, contradicting the "never touch, warn loudly"
    # invariant documented above.
    if [[ -n "$tracked_dirty" ]]; then
        echo "fleet-clone-freshness: $root has tracked modifications on '$branch' — leaving it alone (live WIP wins). Claims stay blocked until it is clean and on master; commit or ship the WIP, then rerun fleet-up." >&2
        return 0
    fi

    if [[ "$branch" != "master" ]]; then
        if git -C "$root" checkout master --quiet 2>/dev/null; then
            echo "fleet-clone-freshness: $root returned to master (was on '$branch', no tracked WIP)." >&2
        else
            echo "fleet-clone-freshness: $root checkout master failed (was on '$branch') — leaving as-is (human fixup needed)." >&2
            return 0
        fi
    fi

    git -C "$root" fetch origin master --quiet 2>/dev/null || true
    _ff_advance_to_origin_master "$root"
    return 0
}
