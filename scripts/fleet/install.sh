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
# Optionally appends a marked snippet to ~/.zshrc for fleet-run tab
# completion (see --no-zshrc). If ~/bin is not on PATH, prints the line
# to add for each common shell.

set -euo pipefail

# --- Windows (MSYS2 / Git Bash) symlink support -----------------------------
# The fleet scripts and ir-* tools resolve their sibling helpers through their
# *real* on-disk location (BASH_SOURCE symlink-walk + ../lib /
# ../../engine/tools/bin). A plain MSYS2 `ln -s` COPIES by default, which
# breaks that relative resolution — so on Windows we require real symlinks.
# Native NTFS symlinks need Windows Developer Mode (or an elevated shell);
# winsymlinks:nativestrict makes `ln` fail loudly instead of silently copying.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        export MSYS=winsymlinks:nativestrict
        export CYGWIN=winsymlinks:nativestrict
        _probe_dir="$(mktemp -d)"
        if ln -s "$_probe_dir/nonexistent-target" "$_probe_dir/link" 2>/dev/null \
            && [[ -L "$_probe_dir/link" ]]; then
            rm -rf "$_probe_dir"
            echo "note: native symlinks enabled (Windows)."
        else
            rm -rf "$_probe_dir" 2>/dev/null || true
            cat >&2 <<'EOF'
install.sh: this Windows shell cannot create native symlinks, which the fleet
            tooling requires (sibling-helper resolution breaks under copies).
            Two fixes:

  1. Enable Developer Mode (Settings > For developers > Developer Mode),
     open a fresh terminal, and re-run install.sh; OR
  2. Skip install.sh for the executables and put the repo's tool dirs on
     PATH instead — the scripts run from their real location, no symlinks:
         export PATH="<repo>/scripts/fleet:<repo>/engine/tools/bin:$PATH"
     (add to ~/.bashrc). Role files still want symlinking into
     ~/.claude/commands; re-run install.sh after enabling Developer Mode.
EOF
            exit 1
        fi
        ;;
esac

INSTALL_APPEND_ZSHRC=1
[[ -n "${IRREDEN_INSTALL_SKIP_ZSHRC:-}" ]] && INSTALL_APPEND_ZSHRC=0
INSTALL_APPEND_SSH_CONFIG=1
[[ -n "${IRREDEN_INSTALL_SKIP_SSH_CONFIG:-}" ]] && INSTALL_APPEND_SSH_CONFIG=0
for arg in "$@"; do
    case "$arg" in
        --no-zshrc)
            INSTALL_APPEND_ZSHRC=0
            ;;
        --no-ssh-config)
            INSTALL_APPEND_SSH_CONFIG=0
            ;;
        -h | --help)
            cat <<'EOF'
Usage: install.sh [--no-zshrc] [--no-ssh-config]

  Symlinks fleet scripts to ~/bin/, role slash commands to
  ~/.claude/commands/, and shell completions (bash + zsh helpers).

  For zsh, may append a marked two-line block to ~/.zshrc that sources
  ~/.zsh/completions/irreden-fleet.zsh (only if macOS, or login shell is
  zsh, or ~/.zshrc already exists — avoids creating ~/.zshrc on bash-only
  Linux). Skips if the marker is already present (safe to re-run).

  Also applies host protections against GitHub connection black-holes (#2362):
  a marked ssh-keepalive block for Host github.com in ~/.ssh/config, git
  http.lowSpeed* globals, and a ~/bin/timeout shim when no coreutils timeout
  is present. All idempotent; an existing Host github.com block is left alone.

  --no-zshrc       Do not modify ~/.zshrc (still symlinks ~/.zsh/completions/).
                   Same effect: IRREDEN_INSTALL_SKIP_ZSHRC=1 install.sh
  --no-ssh-config  Do not modify ~/.ssh/config (still sets git globals + shim).
                   Same effect: IRREDEN_INSTALL_SKIP_SSH_CONFIG=1 install.sh
EOF
            exit 0
            ;;
        *)
            echo "install.sh: unknown option: $arg (try --help)" >&2
            exit 1
            ;;
    esac
done

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

# ----------------------------------------------------------------------
# Tool registry — each fleet tool needs THREE coordinated edits below, or
# its ~/bin symlink is silently never created (install reports success;
# the tool only fails "command not found" later, when something invokes it):
#   1. a `<NAME>_SRC` / `<NAME>_DEST` var pair here,
#   2. `<NAME>_SRC` added to the chmod loop ("Ensure the sources are
#      executable" below), and
#   3. a matching `if [[ -f "$<NAME>_SRC" ]]; then ln -sf ... fi` install
#      block under "Step 1: symlink fleet scripts".
# Forgetting (3) — it lives ~100 lines below the var pair — is the easy
# miss (PR #1990: FLEET_PLAN_LINT defined + chmod'd but never symlinked).
# Keep all three in sync when adding or removing a tool.
# ----------------------------------------------------------------------
FLEET_UP_SRC="$SCRIPT_DIR/fleet-up"
FLEET_UP_DEST="$HOME/bin/fleet-up"
FLEET_DOWN_SRC="$SCRIPT_DIR/fleet-down"
FLEET_DOWN_DEST="$HOME/bin/fleet-down"
FLEET_CLAIM_SRC="$SCRIPT_DIR/fleet-claim"
FLEET_CLAIM_DEST="$HOME/bin/fleet-claim"
FLEET_COMMON_SRC="$SCRIPT_DIR/fleet-common.sh"
FLEET_COMMON_DEST="$HOME/bin/fleet-common.sh"
FLEET_CLONE_FRESHNESS_SRC="$SCRIPT_DIR/fleet-clone-freshness.sh"
FLEET_CLONE_FRESHNESS_DEST="$HOME/bin/fleet-clone-freshness.sh"
FLEET_NET_SRC="$SCRIPT_DIR/fleet-net.sh"
FLEET_NET_DEST="$HOME/bin/fleet-net.sh"
FLEET_NET_DOCTOR_SRC="$SCRIPT_DIR/fleet-net-doctor"
FLEET_NET_DOCTOR_DEST="$HOME/bin/fleet-net-doctor"
# timeout-shim.py installs as ~/bin/timeout ONLY when the host has no coreutils
# timeout/gtimeout (host-protection section, step c) — declared here so the
# chmod loop keeps it executable; its symlink is conditional, not in Step 1.
TIMEOUT_SHIM_SRC="$SCRIPT_DIR/timeout-shim.py"
TIMEOUT_SHIM_DEST="$HOME/bin/timeout"
FLEET_BUILD_SRC="$SCRIPT_DIR/fleet-build"
FLEET_BUILD_DEST="$HOME/bin/fleet-build"
FLEET_DEBUG_SRC="$SCRIPT_DIR/fleet-debug"
FLEET_DEBUG_DEST="$HOME/bin/fleet-debug"
FLEET_RUN_SRC="$SCRIPT_DIR/fleet-run"
FLEET_RUN_DEST="$HOME/bin/fleet-run"
FLEET_RUN_TARGETS_SRC="$SCRIPT_DIR/fleet-run-targets"
FLEET_RUN_TARGETS_DEST="$HOME/bin/fleet-run-targets"
FLEET_HELP_SRC="$SCRIPT_DIR/fleet-help"
FLEET_HELP_DEST="$HOME/bin/fleet-help"
FLEET_BABYSIT_SRC="$SCRIPT_DIR/fleet-babysit"
FLEET_BABYSIT_DEST="$HOME/bin/fleet-babysit"
FLEET_LABELS_SRC="$SCRIPT_DIR/fleet-labels"
FLEET_LABELS_DEST="$HOME/bin/fleet-labels"
FLEET_TRANSITION_SRC="$SCRIPT_DIR/fleet-transition"
FLEET_TRANSITION_DEST="$HOME/bin/fleet-transition"
FLEET_REVIEW_VERDICT_SRC="$SCRIPT_DIR/fleet-review-verdict"
FLEET_REVIEW_VERDICT_DEST="$HOME/bin/fleet-review-verdict"
FLEET_HEARTBEAT_SRC="$SCRIPT_DIR/fleet-heartbeat"
FLEET_HEARTBEAT_DEST="$HOME/bin/fleet-heartbeat"
FLEET_STREAM_SRC="$SCRIPT_DIR/fleet-claude-stream"
FLEET_STREAM_DEST="$HOME/bin/fleet-claude-stream"
FLEET_SCOUT_SRC="$SCRIPT_DIR/fleet-state-scout"
FLEET_SCOUT_DEST="$HOME/bin/fleet-state-scout"
FLEET_FEEDBACK_SRC="$SCRIPT_DIR/fleet-feedback"
FLEET_FEEDBACK_DEST="$HOME/bin/fleet-feedback"
FLEET_BUSY_SRC="$SCRIPT_DIR/fleet-worktree-busy-branches"
FLEET_BUSY_DEST="$HOME/bin/fleet-worktree-busy-branches"
FLEET_PR_SRC="$SCRIPT_DIR/fleet-pr"
FLEET_PR_DEST="$HOME/bin/fleet-pr"
FLEET_PR_CLEAR_SRC="$SCRIPT_DIR/fleet-pr-clear-feedback-labels"
FLEET_PR_CLEAR_DEST="$HOME/bin/fleet-pr-clear-feedback-labels"
FLEET_PR_CLAIM_FEEDBACK_SRC="$SCRIPT_DIR/fleet-pr-claim-feedback"
FLEET_PR_CLAIM_FEEDBACK_DEST="$HOME/bin/fleet-pr-claim-feedback"
FLEET_PR_DETACH_SRC="$SCRIPT_DIR/fleet-pr-checkout-detached"
FLEET_PR_DETACH_DEST="$HOME/bin/fleet-pr-checkout-detached"
FLEET_PR_AMEND_SRC="$SCRIPT_DIR/fleet-pr-amend-push"
FLEET_PR_AMEND_DEST="$HOME/bin/fleet-pr-amend-push"
FLEET_ISSUE_SRC="$SCRIPT_DIR/fleet-issue"
FLEET_ISSUE_DEST="$HOME/bin/fleet-issue"
# fleet-gh-poll is a CLI (symlinked); its fleet_gh_poll.py module is NOT
# symlinked — like fleet_blocked_by.py it resolves via the caller's
# sys.path.insert(dirname(abspath(__file__))), which follows the ~/bin symlink
# back to the real scripts/fleet dir.
FLEET_GH_POLL_SRC="$SCRIPT_DIR/fleet-gh-poll"
FLEET_GH_POLL_DEST="$HOME/bin/fleet-gh-poll"
FLEET_DISPATCHER_SRC="$SCRIPT_DIR/fleet-dispatcher"
FLEET_DISPATCHER_DEST="$HOME/bin/fleet-dispatcher"
FLEET_DISPATCH_WRAP_SRC="$SCRIPT_DIR/fleet-dispatch-wrap"
FLEET_DISPATCH_WRAP_DEST="$HOME/bin/fleet-dispatch-wrap"
FLEET_GATE_STATUS_SRC="$SCRIPT_DIR/fleet-gate-status"
FLEET_GATE_STATUS_DEST="$HOME/bin/fleet-gate-status"
FLEET_QUEUE_INGEST_SRC="$SCRIPT_DIR/fleet-queue-ingest"
FLEET_QUEUE_INGEST_DEST="$HOME/bin/fleet-queue-ingest"
FLEET_PLAN_LINT_SRC="$SCRIPT_DIR/fleet-plan-lint"
FLEET_PLAN_LINT_DEST="$HOME/bin/fleet-plan-lint"
FLEET_QUEUE_LIST_SRC="$SCRIPT_DIR/fleet-queue-list"
FLEET_QUEUE_LIST_DEST="$HOME/bin/fleet-queue-list"
FLEET_DECISIONS_SRC="$SCRIPT_DIR/fleet-decisions"
FLEET_DECISIONS_DEST="$HOME/bin/fleet-decisions"
FLEET_NOTIFY_SRC="$SCRIPT_DIR/fleet-notify"
FLEET_NOTIFY_DEST="$HOME/bin/fleet-notify"
FLEET_DIGEST_TICK_SRC="$SCRIPT_DIR/fleet-digest-tick"
FLEET_DIGEST_TICK_DEST="$HOME/bin/fleet-digest-tick"
FLEET_RECONCILE_AMENDMENTS_SRC="$SCRIPT_DIR/fleet-reconcile-amendments"
FLEET_RECONCILE_AMENDMENTS_DEST="$HOME/bin/fleet-reconcile-amendments"
FLEET_ITERATION_SUMMARY_SRC="$SCRIPT_DIR/fleet-iteration-summary"
FLEET_ITERATION_SUMMARY_DEST="$HOME/bin/fleet-iteration-summary"
FLEET_EDIT_SRC="$SCRIPT_DIR/fleet-edit"
FLEET_EDIT_DEST="$HOME/bin/fleet-edit"
FLEET_NIT_CLOSE_SRC="$SCRIPT_DIR/fleet-nit-close-for-pr"
FLEET_NIT_CLOSE_DEST="$HOME/bin/fleet-nit-close-for-pr"
FLEET_VALIDATE_STACK_SRC="$SCRIPT_DIR/fleet-validate-stack"
FLEET_VALIDATE_STACK_DEST="$HOME/bin/fleet-validate-stack"
FLEET_EPIC_STATUS_SRC="$SCRIPT_DIR/fleet-epic-status"
FLEET_EPIC_STATUS_DEST="$HOME/bin/fleet-epic-status"
FLEET_VALIDATE_ROLES_SRC="$SCRIPT_DIR/fleet-validate-roles"
FLEET_VALIDATE_ROLES_DEST="$HOME/bin/fleet-validate-roles"
FLEET_ASSERT_WT_SRC="$SCRIPT_DIR/fleet-assert-worktree"
FLEET_ASSERT_WT_DEST="$HOME/bin/fleet-assert-worktree"
WITNESS_SRC="$SCRIPT_DIR/witness"
WITNESS_DEST="$HOME/bin/witness"
SOLO_ARCHITECT_SRC="$SCRIPT_DIR/solo-architect"
SOLO_ARCHITECT_DEST="$HOME/bin/solo-architect"
FLEET_REBASE_SRC="$SCRIPT_DIR/fleet-rebase"
FLEET_REBASE_DEST="$HOME/bin/fleet-rebase"
FLEET_GH_TOKEN_SRC="$SCRIPT_DIR/fleet-gh-token"
FLEET_GH_TOKEN_DEST="$HOME/bin/fleet-gh-token"
FLEET_SESSION_TRACK_SRC="$SCRIPT_DIR/fleet-session-track"
FLEET_SESSION_TRACK_DEST="$HOME/bin/fleet-session-track"

if [[ ! -f "$FLEET_UP_SRC" ]]; then
    echo "install.sh: $FLEET_UP_SRC does not exist — repo is incomplete" >&2
    exit 1
fi

# Ensure the sources are executable. Git normally preserves the +x bit,
# but if someone unpacked a tarball or checked out with core.fileMode
# off, fix it here.
for src in "$FLEET_UP_SRC" "$FLEET_DOWN_SRC" "$FLEET_CLAIM_SRC" "$FLEET_COMMON_SRC" "$FLEET_CLONE_FRESHNESS_SRC" "$FLEET_NET_SRC" "$FLEET_NET_DOCTOR_SRC" "$TIMEOUT_SHIM_SRC" "$FLEET_BUILD_SRC" "$FLEET_DEBUG_SRC" "$FLEET_RUN_SRC" "$FLEET_RUN_TARGETS_SRC" "$FLEET_HELP_SRC" "$FLEET_BABYSIT_SRC" "$FLEET_LABELS_SRC" "$FLEET_TRANSITION_SRC" "$FLEET_REVIEW_VERDICT_SRC" "$FLEET_HEARTBEAT_SRC" "$FLEET_STREAM_SRC" "$FLEET_SCOUT_SRC" "$FLEET_FEEDBACK_SRC" "$FLEET_BUSY_SRC" "$FLEET_PR_SRC" "$FLEET_PR_CLEAR_SRC" "$FLEET_PR_CLAIM_FEEDBACK_SRC" "$FLEET_PR_DETACH_SRC" "$FLEET_PR_AMEND_SRC" "$FLEET_ISSUE_SRC" "$FLEET_GH_POLL_SRC" "$FLEET_DISPATCHER_SRC" "$FLEET_DISPATCH_WRAP_SRC" "$FLEET_GATE_STATUS_SRC" "$FLEET_QUEUE_INGEST_SRC" "$FLEET_PLAN_LINT_SRC" "$FLEET_QUEUE_LIST_SRC" "$FLEET_DECISIONS_SRC" "$FLEET_NOTIFY_SRC" "$FLEET_DIGEST_TICK_SRC" "$FLEET_RECONCILE_AMENDMENTS_SRC" "$FLEET_ITERATION_SUMMARY_SRC" "$FLEET_EDIT_SRC" "$FLEET_NIT_CLOSE_SRC" "$FLEET_VALIDATE_STACK_SRC" "$FLEET_EPIC_STATUS_SRC" "$FLEET_VALIDATE_ROLES_SRC" "$FLEET_ASSERT_WT_SRC" "$WITNESS_SRC" "$SOLO_ARCHITECT_SRC" "$FLEET_REBASE_SRC" "$FLEET_GH_TOKEN_SRC" "$FLEET_SESSION_TRACK_SRC"; do
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

if [[ -f "$FLEET_DOWN_SRC" ]]; then
    ln -sf "$FLEET_DOWN_SRC" "$FLEET_DOWN_DEST"
    echo "symlinked $FLEET_DOWN_DEST -> $FLEET_DOWN_SRC"
fi

if [[ -f "$FLEET_CLAIM_SRC" ]]; then
    ln -sf "$FLEET_CLAIM_SRC" "$FLEET_CLAIM_DEST"
    echo "symlinked $FLEET_CLAIM_DEST -> $FLEET_CLAIM_SRC"
fi

if [[ -f "$FLEET_COMMON_SRC" ]]; then
    ln -sf "$FLEET_COMMON_SRC" "$FLEET_COMMON_DEST"
    echo "symlinked $FLEET_COMMON_DEST -> $FLEET_COMMON_SRC"
fi

# fleet-clone-freshness.sh must be symlinked alongside the other fleet scripts so
# the by-dir source pattern (fleet-up) and the FLEET_LIB_DIR source (dispatcher /
# fleet-claim) both resolve it through ~/bin. (#1810)
if [[ -f "$FLEET_CLONE_FRESHNESS_SRC" ]]; then
    ln -sf "$FLEET_CLONE_FRESHNESS_SRC" "$FLEET_CLONE_FRESHNESS_DEST"
    echo "symlinked $FLEET_CLONE_FRESHNESS_DEST -> $FLEET_CLONE_FRESHNESS_SRC"
fi

# fleet-net.sh (the network-timeout guard, #2362) is a sourced lib like
# fleet-common.sh — symlink it into ~/bin so the FLEET_LIB_DIR source pattern in
# fleet-rebase / fleet-claim / fleet-dispatcher resolves it through ~/bin.
if [[ -f "$FLEET_NET_SRC" ]]; then
    ln -sf "$FLEET_NET_SRC" "$FLEET_NET_DEST"
    echo "symlinked $FLEET_NET_DEST -> $FLEET_NET_SRC"
fi

if [[ -f "$FLEET_NET_DOCTOR_SRC" ]]; then
    ln -sf "$FLEET_NET_DOCTOR_SRC" "$FLEET_NET_DOCTOR_DEST"
    echo "symlinked $FLEET_NET_DOCTOR_DEST -> $FLEET_NET_DOCTOR_SRC"
fi

if [[ -f "$FLEET_BUILD_SRC" ]]; then
    ln -sf "$FLEET_BUILD_SRC" "$FLEET_BUILD_DEST"
    echo "symlinked $FLEET_BUILD_DEST -> $FLEET_BUILD_SRC"
fi

if [[ -f "$FLEET_DEBUG_SRC" ]]; then
    ln -sf "$FLEET_DEBUG_SRC" "$FLEET_DEBUG_DEST"
    echo "symlinked $FLEET_DEBUG_DEST -> $FLEET_DEBUG_SRC"
fi

if [[ -f "$FLEET_RUN_SRC" ]]; then
    ln -sf "$FLEET_RUN_SRC" "$FLEET_RUN_DEST"
    echo "symlinked $FLEET_RUN_DEST -> $FLEET_RUN_SRC"
fi

if [[ -f "$FLEET_RUN_TARGETS_SRC" ]]; then
    ln -sf "$FLEET_RUN_TARGETS_SRC" "$FLEET_RUN_TARGETS_DEST"
    echo "symlinked $FLEET_RUN_TARGETS_DEST -> $FLEET_RUN_TARGETS_SRC"
fi

if [[ -f "$FLEET_HELP_SRC" ]]; then
    ln -sf "$FLEET_HELP_SRC" "$FLEET_HELP_DEST"
    echo "symlinked $FLEET_HELP_DEST -> $FLEET_HELP_SRC"
fi

if [[ -f "$FLEET_BABYSIT_SRC" ]]; then
    ln -sf "$FLEET_BABYSIT_SRC" "$FLEET_BABYSIT_DEST"
    echo "symlinked $FLEET_BABYSIT_DEST -> $FLEET_BABYSIT_SRC"
fi

if [[ -f "$FLEET_LABELS_SRC" ]]; then
    ln -sf "$FLEET_LABELS_SRC" "$FLEET_LABELS_DEST"
    echo "symlinked $FLEET_LABELS_DEST -> $FLEET_LABELS_SRC"
fi

if [[ -f "$FLEET_TRANSITION_SRC" ]]; then
    ln -sf "$FLEET_TRANSITION_SRC" "$FLEET_TRANSITION_DEST"
    echo "symlinked $FLEET_TRANSITION_DEST -> $FLEET_TRANSITION_SRC"
fi

if [[ -f "$FLEET_REVIEW_VERDICT_SRC" ]]; then
    ln -sf "$FLEET_REVIEW_VERDICT_SRC" "$FLEET_REVIEW_VERDICT_DEST"
    echo "symlinked $FLEET_REVIEW_VERDICT_DEST -> $FLEET_REVIEW_VERDICT_SRC"
fi

if [[ -f "$FLEET_HEARTBEAT_SRC" ]]; then
    ln -sf "$FLEET_HEARTBEAT_SRC" "$FLEET_HEARTBEAT_DEST"
    echo "symlinked $FLEET_HEARTBEAT_DEST -> $FLEET_HEARTBEAT_SRC"
fi

if [[ -f "$FLEET_STREAM_SRC" ]]; then
    ln -sf "$FLEET_STREAM_SRC" "$FLEET_STREAM_DEST"
    echo "symlinked $FLEET_STREAM_DEST -> $FLEET_STREAM_SRC"
fi

if [[ -f "$FLEET_SCOUT_SRC" ]]; then
    ln -sf "$FLEET_SCOUT_SRC" "$FLEET_SCOUT_DEST"
    echo "symlinked $FLEET_SCOUT_DEST -> $FLEET_SCOUT_SRC"
fi

if [[ -f "$FLEET_FEEDBACK_SRC" ]]; then
    ln -sf "$FLEET_FEEDBACK_SRC" "$FLEET_FEEDBACK_DEST"
    echo "symlinked $FLEET_FEEDBACK_DEST -> $FLEET_FEEDBACK_SRC"
fi

if [[ -f "$FLEET_BUSY_SRC" ]]; then
    ln -sf "$FLEET_BUSY_SRC" "$FLEET_BUSY_DEST"
    echo "symlinked $FLEET_BUSY_DEST -> $FLEET_BUSY_SRC"
fi

if [[ -f "$FLEET_PR_SRC" ]]; then
    ln -sf "$FLEET_PR_SRC" "$FLEET_PR_DEST"
    echo "symlinked $FLEET_PR_DEST -> $FLEET_PR_SRC"
fi

if [[ -f "$FLEET_PR_CLEAR_SRC" ]]; then
    ln -sf "$FLEET_PR_CLEAR_SRC" "$FLEET_PR_CLEAR_DEST"
    echo "symlinked $FLEET_PR_CLEAR_DEST -> $FLEET_PR_CLEAR_SRC"
fi

if [[ -f "$FLEET_PR_CLAIM_FEEDBACK_SRC" ]]; then
    ln -sf "$FLEET_PR_CLAIM_FEEDBACK_SRC" "$FLEET_PR_CLAIM_FEEDBACK_DEST"
    echo "symlinked $FLEET_PR_CLAIM_FEEDBACK_DEST -> $FLEET_PR_CLAIM_FEEDBACK_SRC"
fi

if [[ -f "$FLEET_PR_DETACH_SRC" ]]; then
    ln -sf "$FLEET_PR_DETACH_SRC" "$FLEET_PR_DETACH_DEST"
    echo "symlinked $FLEET_PR_DETACH_DEST -> $FLEET_PR_DETACH_SRC"
fi

if [[ -f "$FLEET_PR_AMEND_SRC" ]]; then
    ln -sf "$FLEET_PR_AMEND_SRC" "$FLEET_PR_AMEND_DEST"
    echo "symlinked $FLEET_PR_AMEND_DEST -> $FLEET_PR_AMEND_SRC"
fi

if [[ -f "$FLEET_ISSUE_SRC" ]]; then
    ln -sf "$FLEET_ISSUE_SRC" "$FLEET_ISSUE_DEST"
    echo "symlinked $FLEET_ISSUE_DEST -> $FLEET_ISSUE_SRC"
fi

if [[ -f "$FLEET_GH_POLL_SRC" ]]; then
    ln -sf "$FLEET_GH_POLL_SRC" "$FLEET_GH_POLL_DEST"
    echo "symlinked $FLEET_GH_POLL_DEST -> $FLEET_GH_POLL_SRC"
fi

if [[ -f "$FLEET_DISPATCHER_SRC" ]]; then
    ln -sf "$FLEET_DISPATCHER_SRC" "$FLEET_DISPATCHER_DEST"
    echo "symlinked $FLEET_DISPATCHER_DEST -> $FLEET_DISPATCHER_SRC"
fi

if [[ -f "$FLEET_DISPATCH_WRAP_SRC" ]]; then
    ln -sf "$FLEET_DISPATCH_WRAP_SRC" "$FLEET_DISPATCH_WRAP_DEST"
    echo "symlinked $FLEET_DISPATCH_WRAP_DEST -> $FLEET_DISPATCH_WRAP_SRC"
fi

if [[ -f "$FLEET_GATE_STATUS_SRC" ]]; then
    ln -sf "$FLEET_GATE_STATUS_SRC" "$FLEET_GATE_STATUS_DEST"
    echo "symlinked $FLEET_GATE_STATUS_DEST -> $FLEET_GATE_STATUS_SRC"
fi

if [[ -f "$FLEET_QUEUE_INGEST_SRC" ]]; then
    ln -sf "$FLEET_QUEUE_INGEST_SRC" "$FLEET_QUEUE_INGEST_DEST"
    echo "symlinked $FLEET_QUEUE_INGEST_DEST -> $FLEET_QUEUE_INGEST_SRC"
fi

if [[ -f "$FLEET_PLAN_LINT_SRC" ]]; then
    ln -sf "$FLEET_PLAN_LINT_SRC" "$FLEET_PLAN_LINT_DEST"
    echo "symlinked $FLEET_PLAN_LINT_DEST -> $FLEET_PLAN_LINT_SRC"
fi

if [[ -f "$FLEET_QUEUE_LIST_SRC" ]]; then
    ln -sf "$FLEET_QUEUE_LIST_SRC" "$FLEET_QUEUE_LIST_DEST"
    echo "symlinked $FLEET_QUEUE_LIST_DEST -> $FLEET_QUEUE_LIST_SRC"
fi

if [[ -f "$FLEET_DECISIONS_SRC" ]]; then
    ln -sf "$FLEET_DECISIONS_SRC" "$FLEET_DECISIONS_DEST"
    echo "symlinked $FLEET_DECISIONS_DEST -> $FLEET_DECISIONS_SRC"
fi

if [[ -f "$FLEET_NOTIFY_SRC" ]]; then
    ln -sf "$FLEET_NOTIFY_SRC" "$FLEET_NOTIFY_DEST"
    echo "symlinked $FLEET_NOTIFY_DEST -> $FLEET_NOTIFY_SRC"
fi

if [[ -f "$FLEET_DIGEST_TICK_SRC" ]]; then
    ln -sf "$FLEET_DIGEST_TICK_SRC" "$FLEET_DIGEST_TICK_DEST"
    echo "symlinked $FLEET_DIGEST_TICK_DEST -> $FLEET_DIGEST_TICK_SRC"
fi

if [[ -f "$FLEET_RECONCILE_AMENDMENTS_SRC" ]]; then
    ln -sf "$FLEET_RECONCILE_AMENDMENTS_SRC" "$FLEET_RECONCILE_AMENDMENTS_DEST"
    echo "symlinked $FLEET_RECONCILE_AMENDMENTS_DEST -> $FLEET_RECONCILE_AMENDMENTS_SRC"
fi

if [[ -f "$FLEET_ITERATION_SUMMARY_SRC" ]]; then
    ln -sf "$FLEET_ITERATION_SUMMARY_SRC" "$FLEET_ITERATION_SUMMARY_DEST"
    echo "symlinked $FLEET_ITERATION_SUMMARY_DEST -> $FLEET_ITERATION_SUMMARY_SRC"
fi

if [[ -f "$FLEET_EDIT_SRC" ]]; then
    ln -sf "$FLEET_EDIT_SRC" "$FLEET_EDIT_DEST"
    echo "symlinked $FLEET_EDIT_DEST -> $FLEET_EDIT_SRC"
fi

if [[ -f "$FLEET_NIT_CLOSE_SRC" ]]; then
    ln -sf "$FLEET_NIT_CLOSE_SRC" "$FLEET_NIT_CLOSE_DEST"
    echo "symlinked $FLEET_NIT_CLOSE_DEST -> $FLEET_NIT_CLOSE_SRC"
fi

if [[ -f "$FLEET_VALIDATE_STACK_SRC" ]]; then
    ln -sf "$FLEET_VALIDATE_STACK_SRC" "$FLEET_VALIDATE_STACK_DEST"
    echo "symlinked $FLEET_VALIDATE_STACK_DEST -> $FLEET_VALIDATE_STACK_SRC"
fi

if [[ -f "$FLEET_EPIC_STATUS_SRC" ]]; then
    ln -sf "$FLEET_EPIC_STATUS_SRC" "$FLEET_EPIC_STATUS_DEST"
    echo "symlinked $FLEET_EPIC_STATUS_DEST -> $FLEET_EPIC_STATUS_SRC"
fi

if [[ -f "$FLEET_VALIDATE_ROLES_SRC" ]]; then
    ln -sf "$FLEET_VALIDATE_ROLES_SRC" "$FLEET_VALIDATE_ROLES_DEST"
    echo "symlinked $FLEET_VALIDATE_ROLES_DEST -> $FLEET_VALIDATE_ROLES_SRC"
fi

if [[ -f "$FLEET_ASSERT_WT_SRC" ]]; then
    ln -sf "$FLEET_ASSERT_WT_SRC" "$FLEET_ASSERT_WT_DEST"
    echo "symlinked $FLEET_ASSERT_WT_DEST -> $FLEET_ASSERT_WT_SRC"
fi

if [[ -f "$WITNESS_SRC" ]]; then
    ln -sf "$WITNESS_SRC" "$WITNESS_DEST"
    echo "symlinked $WITNESS_DEST -> $WITNESS_SRC"
fi

if [[ -f "$SOLO_ARCHITECT_SRC" ]]; then
    ln -sf "$SOLO_ARCHITECT_SRC" "$SOLO_ARCHITECT_DEST"
    echo "symlinked $SOLO_ARCHITECT_DEST -> $SOLO_ARCHITECT_SRC"
fi

if [[ -f "$FLEET_REBASE_SRC" ]]; then
    ln -sf "$FLEET_REBASE_SRC" "$FLEET_REBASE_DEST"
    echo "symlinked $FLEET_REBASE_DEST -> $FLEET_REBASE_SRC"
fi

if [[ -f "$FLEET_GH_TOKEN_SRC" ]]; then
    ln -sf "$FLEET_GH_TOKEN_SRC" "$FLEET_GH_TOKEN_DEST"
    echo "symlinked $FLEET_GH_TOKEN_DEST -> $FLEET_GH_TOKEN_SRC"
fi

if [[ -f "$FLEET_SESSION_TRACK_SRC" ]]; then
    ln -sf "$FLEET_SESSION_TRACK_SRC" "$FLEET_SESSION_TRACK_DEST"
    echo "symlinked $FLEET_SESSION_TRACK_DEST -> $FLEET_SESSION_TRACK_SRC"
fi

# Engine-level ir-* tools (T-318 sub-task 1). These live under
# engine/tools/bin/ — they're not fleet-specific, but they share the
# install pattern. Symlink whichever ones are present in this checkout
# so the next sub-tasks (ir-build / ir-run migration, ir-perf-grid) can
# add themselves to this loop with a one-line append.
IR_TOOLS_BIN="$REPO_ROOT/engine/tools/bin"
if [[ -d "$IR_TOOLS_BIN" ]]; then
    for ir_src in "$IR_TOOLS_BIN"/ir-*; do
        [[ -f "$ir_src" ]] || continue
        [[ -x "$ir_src" ]] || chmod +x "$ir_src"
        ir_dest="$HOME/bin/$(basename "$ir_src")"
        ln -sf "$ir_src" "$ir_dest"
        echo "symlinked $ir_dest -> $ir_src"
    done
fi

# Bash programmable completion (fleet-run / fleet-build). Loaded by the
# bash-completion package from XDG path on many Linux setups and
# Homebrew bash-completion@2 on macOS.
FLEET_COMP_SRC="$SCRIPT_DIR/completions/fleet-run.bash"
BASH_COMP_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/bash-completion/completions"
if [[ -f "$FLEET_COMP_SRC" ]]; then
    mkdir -p "$BASH_COMP_DIR"
    ln -sf "$FLEET_COMP_SRC" "$BASH_COMP_DIR/fleet-run"
    ln -sf "$FLEET_COMP_SRC" "$BASH_COMP_DIR/fleet-build"
    ln -sf "$FLEET_COMP_SRC" "$BASH_COMP_DIR/fleet-debug"
    ln -sf "$FLEET_COMP_SRC" "$BASH_COMP_DIR/ir-run"
    ln -sf "$FLEET_COMP_SRC" "$BASH_COMP_DIR/ir-build"
    echo "symlinked bash completion -> $BASH_COMP_DIR/fleet-run (+ fleet-build, fleet-debug, ir-run, ir-build)"
fi

# Zsh: bash-completion dirs are not read by zsh; irreden-fleet.zsh uses
# bashcompinit. Same files on every OS; ~/.zshrc is only touched when this
# machine likely uses zsh (see _irreden_install_wants_zsh_rc).
FLEET_ZSH_COMP_SRC="$SCRIPT_DIR/completions/irreden-fleet.zsh"
ZSH_COMP_DIR="$HOME/.zsh/completions"
_irreden_install_wants_zsh_rc() {
    # macOS default login shell is zsh; many users have no ~/.zshrc yet.
    [[ "$(uname -s 2>/dev/null)" == Darwin ]] && return 0
    [[ -f "$HOME/.zshrc" ]] && return 0
    [[ "${SHELL:-}" == */zsh ]] && return 0
    return 1
}

if [[ -f "$FLEET_ZSH_COMP_SRC" && -f "$FLEET_COMP_SRC" ]]; then
    mkdir -p "$ZSH_COMP_DIR"
    ln -sf "$FLEET_COMP_SRC" "$ZSH_COMP_DIR/fleet-run.bash"
    ln -sf "$FLEET_ZSH_COMP_SRC" "$ZSH_COMP_DIR/irreden-fleet.zsh"
    echo "symlinked zsh completion helper -> $ZSH_COMP_DIR/irreden-fleet.zsh (+ fleet-run.bash)"

    # Idempotent: one marked block in ~/.zshrc sources the symlinked helper.
    ZSH_RC="$HOME/.zshrc"
    ZSH_COMP_MARKER="# IRREDEN_ENGINE: fleet-run tab completion (install.sh)"
    if ((INSTALL_APPEND_ZSHRC)) && ! _irreden_install_wants_zsh_rc; then
        echo "note: skipped ~/.zshrc (not macOS, login shell is not zsh, and no ~/.zshrc yet)."
        echo "      zsh helpers are in $ZSH_COMP_DIR — add the source line when you use zsh."
    elif ((INSTALL_APPEND_ZSHRC)); then
        if [[ -e "$ZSH_RC" && -d "$ZSH_RC" ]]; then
            echo "install.sh: $ZSH_RC is a directory — not appending zsh snippet." >&2
        elif [[ -f "$ZSH_RC" ]] && grep -qF "$ZSH_COMP_MARKER" "$ZSH_RC" 2>/dev/null; then
            echo "note: zsh completion snippet already present in $ZSH_RC (marker unchanged)."
        else
            had_zshrc=0
            [[ -f "$ZSH_RC" ]] && had_zshrc=1
            if {
                echo ""
                echo "$ZSH_COMP_MARKER"
                echo "[[ -r $ZSH_COMP_DIR/irreden-fleet.zsh ]] && source $ZSH_COMP_DIR/irreden-fleet.zsh"
            } >>"$ZSH_RC" 2>/dev/null; then
                if ((had_zshrc)); then
                    echo "appended zsh completion snippet to $ZSH_RC"
                else
                    echo "created $ZSH_RC with zsh completion snippet"
                fi
            else
                echo "install.sh: could not write to $ZSH_RC — add this block manually:" >&2
                echo "" >&2
                echo "$ZSH_COMP_MARKER" >&2
                echo "[[ -r $ZSH_COMP_DIR/irreden-fleet.zsh ]] && source $ZSH_COMP_DIR/irreden-fleet.zsh" >&2
            fi
        fi
    else
        echo "note: skipped ~/.zshrc (--no-zshrc or IRREDEN_INSTALL_SKIP_ZSHRC)."
    fi
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

if [[ -f "$FLEET_COMP_SRC" ]]; then
    echo
    echo "Bash tab-completion for fleet-run / fleet-build / fleet-debug / ir-run / ir-build"
    echo "  (built targets and --target names) installs under:"
    echo "    $BASH_COMP_DIR"
    echo "  Open a new bash login shell (or: source your bash-completion init)"
    echo "  so it loads. Ubuntu/Debian: install the \`bash-completion\` package;"
    echo "  many images already source /usr/share/bash-completion/bash_completion from ~/.bashrc."
fi

if [[ -f "$FLEET_ZSH_COMP_SRC" ]]; then
    echo
    echo "zsh: tab completion for fleet-run / fleet-build / ir-run / ir-build uses ~/.zsh/completions/"
    echo "  (bash-completion paths are not loaded by zsh by default.)"
    if ((INSTALL_APPEND_ZSHRC)); then
        echo "  If ~/.zshrc was updated, open a new terminal or \`source ~/.zshrc\`."
    else
        echo "  Add to ~/.zshrc (or re-run without --no-zshrc):"
        echo "    [[ -r $ZSH_COMP_DIR/irreden-fleet.zsh ]] && source $ZSH_COMP_DIR/irreden-fleet.zsh"
    fi
    echo "  Then \`fleet-run IR<Tab>\` lists built executables, not cwd files."
fi

# ----------------------------------------------------------------------
# Step 5: host protections against GitHub connection black-holes (#2362)
# ----------------------------------------------------------------------
# The fleet host's TCP connections to GitHub intermittently die silently (no
# RST), hanging any unguarded network call forever — three fleet-wide outages
# in four days. fleet-net.sh (installed above) bounds every git/gh call inside
# the daemons with a `timeout`; these host-level settings make a black-holed
# connection self-terminate at the transport layer too, and guarantee a
# `timeout` binary exists for the guard. All idempotent; safe to re-run.

# (a) ssh keepalives for github.com — a black-holed ssh session that would
# otherwise hang forever self-terminates in ~60s (interval 15 x count 4).
# Written only as a MARKED block, and ONLY when no Host github.com block exists
# yet: ssh is first-match-wins, so a second block would be silently ignored, and
# merging into a user-owned block is not ours to do.
SSH_CONFIG="$HOME/.ssh/config"
SSH_MARKER="# IRREDEN_ENGINE: github.com ssh keepalives (install.sh, #2362)"
if ((INSTALL_APPEND_SSH_CONFIG)); then
    if [[ -f "$SSH_CONFIG" ]] && grep -qF "$SSH_MARKER" "$SSH_CONFIG" 2>/dev/null; then
        echo "note: github.com ssh keepalives already present in $SSH_CONFIG (marker unchanged)."
    elif [[ -f "$SSH_CONFIG" ]] && grep -qiE '^[[:space:]]*Host[[:space:]].*github\.com' "$SSH_CONFIG" 2>/dev/null; then
        echo "note: $SSH_CONFIG already has a Host github.com block (user-owned) — leaving it alone."
        echo "      For black-hole protection, ensure it sets ServerAliveInterval 15 / ServerAliveCountMax 4 / ConnectTimeout 15."
    else
        # Ensure ~/.ssh (700) exists and, if the config is absent, create it 600
        # before appending — ssh refuses a group/world-writable setup. An
        # existing file keeps its own mode (no chmod of user files).
        ( umask 077; mkdir -p "$HOME/.ssh"; [[ -e "$SSH_CONFIG" ]] || : > "$SSH_CONFIG" ) 2>/dev/null || true
        if {
            echo ""
            echo "$SSH_MARKER"
            echo "Host github.com"
            echo "    ServerAliveInterval 15"
            echo "    ServerAliveCountMax 4"
            echo "    ConnectTimeout 15"
            echo "    IPQoS throughput"
        } >>"$SSH_CONFIG" 2>/dev/null; then
            echo "appended github.com ssh keepalives to $SSH_CONFIG"
        else
            echo "install.sh: could not write $SSH_CONFIG — add a Host github.com block with ServerAliveInterval 15 manually." >&2
        fi
    fi
else
    echo "note: skipped ~/.ssh/config (--no-ssh-config or IRREDEN_INSTALL_SKIP_SSH_CONFIG)."
fi

# (a2) ssh connection multiplexing for github.com — the fleet's git-over-ssh
# churn (a fresh TCP dial per operation, thousands/day) leaks a TIME_WAIT
# socket per dial; on a host whose kernel PCB reaper wedges (2026-07-15
# incident: macOS, 85-day uptime) the leak exhausts the entire ephemeral port
# range and EVERY new outbound connection fails with EADDRNOTAVAIL. One
# persistent master connection drops that churn to ~zero. Appended as its own
# marked block: ssh config is first-match-wins PER PARAMETER, so this composes
# with any earlier github.com block — options the user (or stanza (a)) already
# set win, and only the unset Control* options are picked up from here. If the
# master itself black-holes, stanza (a)'s keepalives kill it in ~60s and the
# next git op re-dials. Diagnose pressure anytime with `fleet-net-doctor`.
SSH_MUX_MARKER="# IRREDEN_ENGINE: github.com ssh connection multiplexing (install.sh)"
if ((INSTALL_APPEND_SSH_CONFIG)); then
    if [[ -f "$SSH_CONFIG" ]] && grep -qF "$SSH_MUX_MARKER" "$SSH_CONFIG" 2>/dev/null; then
        echo "note: github.com ssh multiplexing already present in $SSH_CONFIG (marker unchanged)."
    elif ssh -G github.com 2>/dev/null | grep -qi '^controlmaster auto'; then
        echo "note: github.com already resolves ControlMaster auto (user-owned config) — leaving it alone."
    else
        ( umask 077; mkdir -p "$HOME/.ssh"; [[ -e "$SSH_CONFIG" ]] || : > "$SSH_CONFIG" ) 2>/dev/null || true
        if {
            echo ""
            echo "$SSH_MUX_MARKER"
            echo "Host github.com"
            echo "    ControlMaster auto"
            echo "    ControlPath ~/.ssh/cm-%r@%h:%p"
            echo "    ControlPersist 10m"
        } >>"$SSH_CONFIG" 2>/dev/null; then
            echo "appended github.com ssh connection multiplexing to $SSH_CONFIG"
        else
            echo "install.sh: could not write $SSH_CONFIG — add ControlMaster auto / ControlPersist 10m for Host github.com manually." >&2
        fi
    fi
fi

# (b) Bound git-over-HTTPS: abort a transfer stalled below 1000 B/s for 60s.
# Set only when unset, so an intentional user value is respected.
if command -v git >/dev/null 2>&1; then
    git config --global --get http.lowSpeedLimit >/dev/null 2>&1 || \
        git config --global http.lowSpeedLimit 1000
    git config --global --get http.lowSpeedTime >/dev/null 2>&1 || \
        git config --global http.lowSpeedTime 60
    echo "ensured git http.lowSpeedLimit / http.lowSpeedTime globals (bounds stalled HTTPS transfers)."
fi

# (c) Guarantee a coreutils-compatible `timeout` for fleet-net.sh's guard. When
# the host ships neither `timeout` nor `gtimeout`, install the python shim as
# ~/bin/timeout (its --version advertises "coreutils" so the lib's probe accepts
# it). A host that has real coreutils uses that; the shim is a last resort.
_install_has_coreutils_timeout() {
    local c
    for c in timeout gtimeout; do
        if command -v "$c" >/dev/null 2>&1 && "$c" --version 2>/dev/null | grep -qi coreutils; then
            return 0
        fi
    done
    return 1
}
if _install_has_coreutils_timeout; then
    echo "note: coreutils timeout/gtimeout present — fleet-net.sh network guard is active."
elif [[ -f "$TIMEOUT_SHIM_SRC" ]]; then
    ln -sf "$TIMEOUT_SHIM_SRC" "$TIMEOUT_SHIM_DEST"
    echo "symlinked $TIMEOUT_SHIM_DEST -> $TIMEOUT_SHIM_SRC (no coreutils timeout found; using the fleet shim)"
    echo "note: for a native guard, install GNU coreutils (Linux: apt install coreutils; macOS: brew install coreutils -> gtimeout)."
fi

# Stamp this successful symlink pass so fleet-up / fleet-dispatch-wrap can skip
# re-linking until a fleet source (tool / role-cmd / ir-*) is newer than the
# stamp — see fleet_install_stale in fleet-common.sh (#2262). Overridable for
# tests via FLEET_INSTALL_STAMP.
_install_stamp="${FLEET_INSTALL_STAMP:-$HOME/.fleet/state/.install-stamp}"
mkdir -p "$(dirname "$_install_stamp")" 2>/dev/null || true
touch "$_install_stamp" 2>/dev/null || true

echo
echo "install.sh: done."
