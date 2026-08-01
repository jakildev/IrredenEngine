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
#                                     resets --hard; never switches branches
#                                     except one self-heal case: a clean tree
#                                     parked on the fleet's scratch namespace
#                                     (claude/*-scratch) with no unique commits
#                                     is provably junk from a role's cwd drift,
#                                     so it is put back on master and the ref
#                                     dropped. Every other park — including a
#                                     clean, pushed claude/* feature branch,
#                                     which is exactly what a live Cursor
#                                     session looks like — is left alone and
#                                     escalated instead (#2363).
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
#   FLEET_STATE_DIR               — where the per-repo rate-limit sentinel and
#                                   the persistent-skip counter live (defaults
#                                   to ~/.fleet/state).
#   FLEET_ALERTS_DIR              — where the escalation alert file is dropped
#                                   (defaults to ~/.fleet/alerts, same
#                                   convention as fleet-rebase, #2362).
#   FLEET_FRESHNESS_SKIP_ESCALATE_N
#                                 — consecutive identical skips before the one
#                                   loud line + alert file (default 15).

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

# --- persistent-skip escalation ----------------------------------------------
# advance_main_clone runs unattended on every dispatcher tick, so a guard that
# skips keeps skipping — and a repeating stderr line is not a signal anyone
# reads. Count consecutive identical skips; at the Nth emit one loud line and
# drop a flat alert file (the durable signal), then go quiet until the
# condition clears. See #2363.
#
# Counter: ${FLEET_STATE_DIR:-~/.fleet/state}/.<repo_tag>-freshness-skip
#          one line, "<count> <first_seen_epoch> <reason>|<branch>"
# Alert:   ${FLEET_ALERTS_DIR:-~/.fleet/alerts}/clone-freshness-<repo_tag>
#          one printf'd key=value line, the flat shape fleet-rebase's
#          fleet-rebase-hung-lock established (#2362).
# Every state write is best-effort: a read-only $HOME must never break the
# advance path, and every helper here returns 0 for `set -e` callers.

# Consecutive warn-eligible skips before escalating. Warn-eligible ticks run
# ~1/min (the 60s fetch sentinel gates the rest), so 15 ≈ 15 min of continuous
# skip — sized under the 30-min whole-fleet claim freeze this exists to catch,
# which an hour-scale threshold would have missed entirely.
_freshness_escalate_n() {
    local n="${FLEET_FRESHNESS_SKIP_ESCALATE_N:-15}"
    if [[ ! "$n" =~ ^[0-9]+$ ]] || (( n < 1 )); then
        n=15
    fi
    echo "$n"
}

_freshness_counter_path() {
    echo "${FLEET_STATE_DIR:-$HOME/.fleet/state}/.$(basename "$1")-freshness-skip"
}

_freshness_alert_path() {
    echo "${FLEET_ALERTS_DIR:-$HOME/.fleet/alerts}/clone-freshness-$(basename "$1")"
}

# _freshness_iso8601 <epoch>
# BSD (macOS) spells it `date -r`, GNU spells it `date -d @…`; -r on GNU tries
# to stat a file and fails, so the chain lands on the right one either way.
_freshness_iso8601() {
    date -u -r "$1" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null \
        || date -u -d "@$1" '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null \
        || echo "$1"
}

# _freshness_warn <repo_root> <reason>|<branch> <message>
# Emit a skip warning, escalating-then-quieting on repetition. Same key as the
# last tick → increment; a different key → restart the count, since a changed
# condition is news again.
_freshness_warn() {
    local root="$1" key="$2" msg="$3"
    local counter n now count first prev_key
    counter="$(_freshness_counter_path "$root")"
    n="$(_freshness_escalate_n)"
    now="$(date +%s)"

    count=0
    first="$now"
    prev_key=""
    if [[ -f "$counter" ]]; then
        read -r count first prev_key < "$counter" 2>/dev/null || true
    fi
    [[ "${count:-}" =~ ^[0-9]+$ ]] || count=0
    [[ "${first:-}" =~ ^[0-9]+$ ]] || first="$now"
    if [[ "${prev_key:-}" != "$key" ]]; then
        count=0
        first="$now"
    fi
    count=$(( count + 1 ))

    mkdir -p "$(dirname "$counter")" 2>/dev/null || true
    printf '%s %s %s\n' "$count" "$first" "$key" > "$counter" 2>/dev/null || true

    # Below the threshold: the ordinary per-tick warn, no alert yet.
    if (( count < n )); then
        echo "$msg" >&2
        return 0
    fi

    local alert reason branch since remedy
    alert="$(_freshness_alert_path "$root")"
    reason="${key%%|*}"
    branch="${key#*|}"
    since="$(_freshness_iso8601 "$first")"
    remedy="git -C $root checkout master && git -C $root merge --ff-only origin/master  (or rerun fleet-up)"
    # stderr goes quiet after the one loud escalation, so the alert is the only
    # standing signal — hence rewritten every tick past N, not stamped once at
    # it. Write-once would freeze count= at the escalation instant, and would
    # let a human triaging the alerts inbox silence a still-live condition
    # forever (warns stay suppressed, so nothing recreates the file). Only the
    # flat one-line alert artifact is shared with fleet-rebase's
    # `fleet-rebase-hung-lock` (#2362); the counter/quiet cycle around it is
    # this function's own (see #2795).
    if (( count == n )); then
        echo "$msg" >&2
        echo "fleet-clone-freshness: ESCALATION — $root has skipped its advance $count consecutive times (reason=$reason branch='$branch') since $since. Wrote $alert. Remedy: $remedy. Suppressing further identical warns until the condition clears." >&2
    fi
    mkdir -p "$(dirname "$alert")" 2>/dev/null || true
    printf "clone-freshness skip: host=%s root=%s branch=%s reason=%s count=%s since=%s remedy='%s'\n" \
        "$(hostname 2>/dev/null || echo '?')" "$root" "$branch" "$reason" "$count" "$since" "$remedy" \
        > "$alert" 2>/dev/null || true
    return 0
}

# _freshness_all_clear <repo_root>
# The condition cleared (advanced, or already current on master): drop the
# counter and any alert file so the next persistent skip escalates from scratch.
_freshness_all_clear() {
    rm -f "$(_freshness_counter_path "$1")" "$(_freshness_alert_path "$1")" 2>/dev/null || true
    return 0
}

# advance_main_clone <repo_root>
# Guarded, rate-limited fast-forward of the main clone's master. Safe on the
# shared main checkout: advances ONLY when the clone is on branch master, has a
# clean working tree, and master is a strict pure-ancestor of origin/master
# (i.e. a real fast-forward). Any guard miss → skip + warn, never mutate —
# except the scratch-namespace self-heal in guard 1 (see comment there). Mirrors
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
    # Exception: the fleet's scratch namespace, claude/*-scratch — the pool
    # worktrees' per-iteration scratch refs (claude/pool-<N>-scratch, its
    # claude/game-pool-<N>-scratch twin, the legacy claude/<role>-reviewer-scratch).
    # A scratch ref only ever belongs in .claude/worktrees/*, so one parked HERE
    # is junk left by a role whose shell cwd drifted into the main clone, and it
    # freezes master (blocking every claim on this repo via assert_clone_fresh)
    # until fixed. Self-heal: clean tree AND a HEAD already contained by
    # origin/master (no unique commits, so the ref holds nothing) → back to
    # master, drop the junk ref, fall through to the normal advance.
    #
    # Everything else is left alone and escalated via the skip counter instead —
    # INCLUDING a clean claude/* branch whose HEAD is pushed. "Recoverable" is
    # not "idle": FLEET.md rule 1 puts Cursor / ad-hoc sessions on
    # claude/<area>-<topic>, and commit-and-push leaves exactly that session
    # clean-and-pushed while the human sits on the PR. Healing it would send
    # their next commit to master (violating rule 1) or split the slice across
    # two PRs, with no notice the human ever sees. Proving git loses nothing is
    # a different invariant from proving no session is using the checkout; only
    # the scratch namespace establishes the latter. See #2363.
    local branch dirty checkout_failed_msg ref_note
    branch="$(git -C "$root" rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
    if [[ "$branch" != "master" ]]; then
        dirty="$(git -C "$root" status --porcelain 2>/dev/null || echo SKIP)"
        checkout_failed_msg="fleet-clone-freshness: $root parked on '$branch' but 'git checkout master' failed — master is FROZEN and every claim on this repo will be refused until a human fixes it (git -C $root checkout master)."
        if [[ "$branch" == claude/*-scratch && -z "$dirty" ]] \
            && git -C "$root" merge-base --is-ancestor HEAD origin/master 2>/dev/null; then
            if git -C "$root" checkout master --quiet 2>/dev/null; then
                # The delete is best-effort: `branch -D` refuses a ref another
                # worktree holds (claude/pool-<N>-scratch is exactly that) or
                # one whose refs/heads entry a killed git process left locked.
                # The freeze is already cleared by then, so report which of the
                # two happened rather than claiming a delete git refused — the
                # operator reading this mid-outage acts on the ref.
                if git -C "$root" branch -D "$branch" --quiet 2>/dev/null; then
                    ref_note="deleted the junk branch"
                else
                    ref_note="left the junk branch in place (delete refused — a worktree holds it or the ref is locked)"
                fi
                echo "fleet-clone-freshness: $root was parked on scratch branch '$branch' (clean tree, no unique commits) — self-healed back to master and $ref_note." >&2
                branch=master
            else
                _freshness_warn "$root" "checkout-failed|$branch" "$checkout_failed_msg"
                return 0
            fi
        else
            _freshness_warn "$root" "parked|$branch" "fleet-clone-freshness: $root is on '$branch' (not master) — skipping advance. master is FROZEN and every claim on this repo will be refused until it is put back (git -C $root checkout master)."
            return 0
        fi
    fi
    # Guard 2: clean working tree (never clobber uncommitted work).
    dirty="$(git -C "$root" status --porcelain 2>/dev/null || echo SKIP)"
    if [[ -n "$dirty" && "$dirty" != "SKIP" ]]; then
        _freshness_warn "$root" "dirty|master" "fleet-clone-freshness: $root has uncommitted changes — skipping advance."
        return 0
    fi
    # Guard 3 + the ff itself live in the shared tail. The second arg opts this
    # (unattended, every-tick) path into skip counting; the fleet-up one-shot
    # deliberately does not count.
    _ff_advance_to_origin_master "$root" count
    return 0
}

# _ff_advance_to_origin_master <repo_root> [count_skips]
# Shared tail of advance_main_clone / restore_main_clone_to_master: guarded
# ff-only advance of an on-master clone whose origin/master ref is current
# (callers own the fetch). Diverged or up-to-date → no-op. Always returns 0.
#
# count_skips (non-empty) routes the diverged warn through the escalate-then-
# quiet counter and clears it on a healthy pass; unset, the tail just warns and
# keeps no state. Only advance_main_clone passes it: restore_main_clone_to_master
# is a fleet-up one-shot, so counting there would double-count or spuriously
# clear.
_ff_advance_to_origin_master() {
    local root="$1" count_skips="${2:-}"
    if ! git -C "$root" merge-base --is-ancestor master origin/master 2>/dev/null; then
        local diverged_msg="fleet-clone-freshness: $root master has diverged from origin/master — skipping advance (human fixup needed)."
        if [[ -n "$count_skips" ]]; then
            _freshness_warn "$root" "diverged|master" "$diverged_msg"
        else
            echo "$diverged_msg" >&2
        fi
        return 0
    fi
    local behind
    behind="$(git -C "$root" rev-list --count master..origin/master 2>/dev/null || echo 0)"
    if [[ "${behind:-0}" -le 0 ]]; then
        # Already current (or ahead — left to the human): a healthy pass.
        if [[ -n "$count_skips" ]]; then
            _freshness_all_clear "$root"
        fi
        return 0
    fi
    # ff-only refuses on its own rather than clobber a tracked modification
    # that overlaps the incoming commits; a disjoint dirty tree advances fine.
    if git -C "$root" merge --ff-only origin/master --quiet 2>/dev/null; then
        echo "fleet-clone-freshness: advanced $root master by $behind commit(s) to origin/master." >&2
        if [[ -n "$count_skips" ]]; then
            _freshness_all_clear "$root"
        fi
    else
        # Transient (concurrent git op) — deliberately neither counted nor
        # cleared; the next tick resolves it either way.
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
