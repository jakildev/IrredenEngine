#!/usr/bin/env bash
# scripts/fleet/tests/test_install_role_collision.sh — install.sh role-link
# collision guard.
#
# install.sh Step 2 links the engine's role-*.md into ~/.claude/commands and
# Step 3 links the game's into the SAME directory with `ln -sf`. A basename
# present in both trees let the game link overwrite the engine link on every
# run, so /role-epic-steward loaded the game wrapper in every engine pane. The
# fix keeps the engine link and prints a `note:` naming the collision.
#
# Running the real install.sh needs the whole tool registry, so this suite
# extracts the two role-link steps (Step 2 through the line before Step 3b)
# from the install.sh beside it and runs them against a fixture tree and a
# throwaway $HOME — the same install.sh text, no network, no ~/bin writes.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
INSTALL_SH="$SCRIPT_DIR/install.sh"

source "$(dirname "$0")/lib_assert.sh"

if [[ ! -f "$INSTALL_SH" ]]; then
    echo "SKIP: $INSTALL_SH not found" >&2
    exit 3  # skip status — a missing subject must not score as a pass
fi

TMPROOT=$(mktemp -d)
trap 'rm -rf "$TMPROOT"' EXIT

# The role-link steps, lifted verbatim from install.sh.
SNIPPET="$TMPROOT/role-steps.sh"
awk '/^# Step 2: /{on=1} /^# Step 3b: /{on=0} on' "$INSTALL_SH" > "$SNIPPET"
if [[ ! -s "$SNIPPET" ]] || ! grep -q 'GAME_ROLES' "$SNIPPET"; then
    bad "could not extract the Step 2/3 role-link block from install.sh"
    summarize "install role-collision tests"
    exit $?
fi

# run_steps <repo-root> <home> — run the extracted steps, print combined output.
run_steps() {
    REPO_ROOT="$1" HOME="$2" bash -euo pipefail "$SNIPPET" 2>&1
}

make_fixture() {  # make_fixture <name> — prints the fixture repo root
    local root="$TMPROOT/$1/repo"
    mkdir -p "$root/.claude/commands" "$root/creations/game/.claude/commands" "$TMPROOT/$1/home"
    echo "$root"
}

echo "== collision: engine + game both carry role-twin.md =="
ROOT=$(make_fixture collide)
HOME_DIR="$TMPROOT/collide/home"
echo engine > "$ROOT/.claude/commands/role-twin.md"
echo engine > "$ROOT/.claude/commands/role-worker.md"
echo game   > "$ROOT/creations/game/.claude/commands/role-twin.md"
echo game   > "$ROOT/creations/game/.claude/commands/role-game-architect.md"

OUT=$(run_steps "$ROOT" "$HOME_DIR")
LINK="$HOME_DIR/.claude/commands/role-twin.md"
assert_eq "$(readlink "$LINK")" "$ROOT/.claude/commands/role-twin.md" \
    "colliding basename resolves to the engine copy"
assert_contains "$OUT" "note: game role role-twin.md collides" \
    "collision is reported with a note: line"
assert_eq "$(readlink "$HOME_DIR/.claude/commands/role-game-architect.md")" \
    "$ROOT/creations/game/.claude/commands/role-game-architect.md" \
    "non-colliding game role still links to the game copy"
assert_eq "$(readlink "$HOME_DIR/.claude/commands/role-worker.md")" \
    "$ROOT/.claude/commands/role-worker.md" \
    "engine-only role links to the engine copy"

echo "== self-heal: a stale game link from an earlier run is repointed =="
ln -sf "$ROOT/creations/game/.claude/commands/role-twin.md" "$LINK"
run_steps "$ROOT" "$HOME_DIR" >/dev/null
assert_eq "$(readlink "$LINK")" "$ROOT/.claude/commands/role-twin.md" \
    "re-run repoints a game-owned link back at the engine copy"

echo "== no collision: no note: line =="
ROOT2=$(make_fixture clean)
HOME2="$TMPROOT/clean/home"
echo engine > "$ROOT2/.claude/commands/role-worker.md"
echo game   > "$ROOT2/creations/game/.claude/commands/role-game-architect.md"
OUT2=$(run_steps "$ROOT2" "$HOME2")
assert_absent "$OUT2" "collides" "distinct basenames print no collision note"

summarize "install role-collision tests"
