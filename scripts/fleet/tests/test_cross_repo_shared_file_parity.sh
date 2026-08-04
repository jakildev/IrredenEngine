#!/usr/bin/env bash
# Executes the byte-identity contract declared in
# .github/workflows/auto-rereview.yml's header comment: that file and
# scripts/fleet/classify-auto-rereview.sh must stay byte-identical between
# this repo and its downstream fleet-enabled repo, so both share one
# re-review flow (#2827).
#
# Before this suite, nothing checked the contract — a one-sided edit
# (#2652) drifted the workflow file for 2 days with no signal. It was only
# caught because an unrelated downstream PR happened to re-sync the prose
# by hand.
#
# The downstream repo is only visible on a fleet dev host, where it is
# checked out as a sibling worktree (creations/game/) — CI does not have
# it (this repo is private and not checked out in the engine's Actions
# runners). When the sibling is absent, SKIP rather than reporting a
# vacuous pass (#2786); this is why the check cannot simply be a CI job.
#
# Compares each declared file's origin/master blob (not the working tree,
# which may be dirty) via `git show`, so the result reflects what's
# actually landed rather than local edits-in-progress.
#
# Add a third shared file by adding one entry to CONTRACT_FILES below —
# no other change needed.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=/dev/null
source "$SCRIPT_DIR/tests/lib_assert.sh"
# shellcheck source=/dev/null
source "$SCRIPT_DIR/fleet-common.sh"

# creations/game is gitignored and only ever checked out under the MAIN
# clone (never per-worktree), so a linked worktree (e.g. a pool worktree)
# must resolve through fleet_main_clone_root rather than its own toplevel —
# same resolution fleet-common.sh's install-freshness check uses to find
# the sibling game repo.
ENGINE_ROOT="$(fleet_main_clone_root)"
GAME_ROOT="${FLEET_GAME_ROOT:-$ENGINE_ROOT/creations/game}"

if [[ ! -d "$GAME_ROOT/.git" && ! -f "$GAME_ROOT/.git" ]]; then
    echo "SKIP: downstream repo not present at $GAME_ROOT" >&2
    exit 3  # skip status — run_all.sh must not count this as a pass (#2786)
fi
if ! git -C "$GAME_ROOT" rev-parse --git-dir &>/dev/null; then
    echo "SKIP: $GAME_ROOT is not a valid git repo" >&2
    exit 3
fi

# The declared contract — see .github/workflows/auto-rereview.yml's header
# comment. Intentionally NOT the full 18-path shared surface: most of that
# (CLAUDE.md, README.md, .claude/skills/*, ...) is per-repo by design
# (docs/design/claude-md-sharing.md, docs/design/skill-sharing.md).
CONTRACT_FILES=(
    ".github/workflows/auto-rereview.yml"
    "scripts/fleet/classify-auto-rereview.sh"
)

TMPROOT=$(mktemp -d)
cleanup() { rm -rf "$TMPROOT"; }
trap cleanup EXIT

for path in "${CONTRACT_FILES[@]}"; do
    engine_blob="$TMPROOT/engine.$(basename "$path")"
    game_blob="$TMPROOT/game.$(basename "$path")"

    if ! git -C "$ENGINE_ROOT" show "origin/master:$path" >"$engine_blob" 2>/dev/null; then
        bad "$path: missing from engine origin/master"
        continue
    fi
    if ! git -C "$GAME_ROOT" show "origin/master:$path" >"$game_blob" 2>/dev/null; then
        bad "$path: missing from downstream origin/master"
        continue
    fi

    if cmp -s "$engine_blob" "$game_blob"; then
        ok "$path: byte-identical"
    else
        bad "$path: DRIFTED between engine and downstream origin/master"
    fi
done

summarize "cross-repo shared-file parity"
