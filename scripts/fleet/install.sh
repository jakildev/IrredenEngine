#!/usr/bin/env bash
# scripts/fleet/install.sh — install fleet tooling on this machine.
#
# Idempotent. Creates ~/bin/fleet-up as a symlink to the versioned
# script in this repo, and refreshes ~/.claude/commands/role-*.md as
# symlinks into the versioned role files (engine + game if present).
#
# Run once per machine after cloning the engine repo. Re-run after
# any git pull that touched scripts/fleet/ or .claude/commands/.
#
# Portable across Linux (WSL/Ubuntu) and macOS. Does not require sudo.
# Does not edit shell startup files — if ~/bin is not on PATH, this
# script prints the exact line to add for each common shell.

set -euo pipefail

# Locate the repo root from the script's own path — this works whether
# install.sh is invoked directly, via a relative path, or via a symlink
# from another directory.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# If we were invoked from inside a linked worktree, `.git` is a file
# (pointing at the main clone's gitdir) rather than a directory. In
# that case we want the symlinks to target the **main clone**, not the
# worktree — worktree paths are ephemeral (fleet-up + start-next-task
# reset them), so symlinking into them would dangle after the next
# branch swap.
#
# Exception: during development of the fleet scripts themselves, the
# main clone may not yet have the new files on master. If the main
# clone's scripts/fleet/fleet-up is missing, fall back to the worktree
# paths and print a warning. The user will need to re-run install.sh
# from the main clone after the PR merges.
if [[ -f "$REPO_ROOT/.git" ]]; then
    MAIN_ROOT="$(git -C "$REPO_ROOT" worktree list --porcelain \
        | awk '/^worktree /{print $2; exit}')"
    if [[ -z "$MAIN_ROOT" || ! -d "$MAIN_ROOT" ]]; then
        echo "install.sh: could not resolve main clone from worktree $REPO_ROOT" >&2
        exit 1
    fi
    if [[ -x "$MAIN_ROOT/scripts/fleet/fleet-up" \
        && -f "$MAIN_ROOT/.claude/commands/role-opus-architect.md" ]]; then
        echo "note: invoked from linked worktree. redirecting symlink targets"
        echo "      to main clone at $MAIN_ROOT."
        REPO_ROOT="$MAIN_ROOT"
        SCRIPT_DIR="$REPO_ROOT/scripts/fleet"
    else
        echo "warning: invoked from linked worktree, and main clone at"
        echo "         $MAIN_ROOT is missing scripts/fleet/fleet-up or role"
        echo "         files. Using worktree paths for now — re-run install.sh"
        echo "         from the main clone after your PR merges so the"
        echo "         symlinks point at stable files." >&2
    fi
fi

if [[ ! -f "$REPO_ROOT/.claude/commands/role-opus-architect.md" ]]; then
    echo "install.sh: expected engine role files under $REPO_ROOT/.claude/commands/" >&2
    echo "            — is this actually the IrredenEngine repo?" >&2
    exit 1
fi

FLEET_UP_SRC="$SCRIPT_DIR/fleet-up"
FLEET_UP_DEST="$HOME/bin/fleet-up"
FLEET_CLAIM_SRC="$SCRIPT_DIR/fleet-claim"
FLEET_CLAIM_DEST="$HOME/bin/fleet-claim"

if [[ ! -f "$FLEET_UP_SRC" ]]; then
    echo "install.sh: $FLEET_UP_SRC does not exist — repo is incomplete" >&2
    exit 1
fi

# Ensure the sources are executable. Git normally preserves the +x bit,
# but if someone unpacked a tarball or checked out with core.fileMode
# off, fix it here.
for src in "$FLEET_UP_SRC" "$FLEET_CLAIM_SRC"; do
    if [[ -f "$src" && ! -x "$src" ]]; then
        chmod +x "$src"
    fi
done

# ----------------------------------------------------------------------
# Step 1: symlink fleet scripts into ~/bin
# ----------------------------------------------------------------------

mkdir -p "$HOME/bin"
ln -sf "$FLEET_UP_SRC" "$FLEET_UP_DEST"
echo "symlinked $FLEET_UP_DEST -> $FLEET_UP_SRC"

if [[ -f "$FLEET_CLAIM_SRC" ]]; then
    ln -sf "$FLEET_CLAIM_SRC" "$FLEET_CLAIM_DEST"
    echo "symlinked $FLEET_CLAIM_DEST -> $FLEET_CLAIM_SRC"
fi

# ----------------------------------------------------------------------
# Step 2: symlink engine role slash commands into ~/.claude/commands
# ----------------------------------------------------------------------

mkdir -p "$HOME/.claude/commands"

shopt -s nullglob
ENGINE_ROLES=("$REPO_ROOT"/.claude/commands/role-*.md)
shopt -u nullglob

if [[ ${#ENGINE_ROLES[@]} -eq 0 ]]; then
    echo "install.sh: no engine role files matched $REPO_ROOT/.claude/commands/role-*.md" >&2
    exit 1
fi

for src in "${ENGINE_ROLES[@]}"; do
    dest="$HOME/.claude/commands/$(basename "$src")"
    ln -sf "$src" "$dest"
    echo "symlinked $dest -> $src"
done

# ----------------------------------------------------------------------
# Step 3: symlink game role slash command if the game repo is present
# ----------------------------------------------------------------------

GAME_CMDS_DIR="$REPO_ROOT/creations/game/.claude/commands"
if [[ -d "$GAME_CMDS_DIR" ]]; then
    shopt -s nullglob
    GAME_ROLES=("$GAME_CMDS_DIR"/role-*.md)
    shopt -u nullglob
    for src in "${GAME_ROLES[@]}"; do
        dest="$HOME/.claude/commands/$(basename "$src")"
        ln -sf "$src" "$dest"
        echo "symlinked $dest -> $src"
    done
    if [[ ${#GAME_ROLES[@]} -eq 0 ]]; then
        echo "note: no game role files found in $GAME_CMDS_DIR"
    fi
else
    echo "note: game commands dir not found at $GAME_CMDS_DIR"
    echo "      (clone jakildev/irreden into creations/game/ and re-run to pick it up.)"
fi

# ----------------------------------------------------------------------
# Step 4: PATH sanity check
# ----------------------------------------------------------------------

case ":$PATH:" in
    *":$HOME/bin:"*)
        echo
        echo "~/bin is on PATH — fleet-up should be runnable from a fresh shell."
        ;;
    *)
        cat <<'EOF'

WARNING: ~/bin is NOT on PATH in the current shell.

Add this line to your shell startup file, then open a new terminal:

    export PATH="$HOME/bin:$PATH"

Which file depends on the shell:

  zsh  (macOS default)    ~/.zprofile
  bash (Linux default)    ~/.bash_profile  (and also source ~/.bashrc from it)
  fish                    ~/.config/fish/config.fish — use: fish_add_path $HOME/bin

Then run 'fleet-up dry-run' to bring the fleet up.
EOF
        ;;
esac

echo
echo "install.sh: done."
