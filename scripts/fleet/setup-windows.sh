#!/usr/bin/env bash
# setup-windows.sh — one-shot, idempotent native-Windows fleet host setup.
#
# Configures a Windows machine to run the Irreden Engine fleet (host key
# `windows`, OpenGL via the windows-debug preset). Run from an **MSYS2 bash**
# shell (the one place tmux lives) — NOT from PowerShell/cmd or Cygwin.
# Claude Code's Bash tool is Git Bash; both share $HOME, so the fleet's
# ~/.fleet state is common to orchestration (MSYS2) and the agent panes
# (Git Bash).
#
# What it does (all idempotent — safe to re-run):
#   1. Checks prerequisites (git, tmux, jq, claude, MSYS2 mingw64 toolchain).
#   2. Clones the engine to a DEDICATED fleet clone (default $HOME/src/
#      IrredenEngine — kept separate from any interactive dev clone so fleet
#      branch-resets / worktrees don't churn the clone you edit in), or fetches
#      if it already exists.
#   3. Writes ~/.fleet/fleet-up.conf and ~/.config/irreden/host.toml sized for
#      this box (CPU budget left for a co-resident WSL fleet, if any).
#   4. Puts the clone's scripts/fleet + engine/tools/bin on PATH via ~/.bashrc
#      (no Developer Mode needed — the scripts resolve their siblings from their
#      real location; Developer-Mode users can run install.sh for ~/bin symlinks
#      instead).
#   5. Creates the fleet worktrees off origin/master (the panes fleet-up drives).
#
# IMPORTANT: the native-Windows build fixes (ir-build cc1plus PATH wrap,
# ir-run .exe/DLL handling, LuaJIT libluajit.a) must be on `master` before the
# worktrees can build. Run this AFTER those land on master.
#
# Override any default via env, e.g.:
#   FLEET_CLONE=/c/work/IrredenEngine FLEET_CPU_BUDGET=32 bash setup-windows.sh
# Full override set (useful when cloning from a fork or a non-default MSYS2 path):
#   FLEET_REPO_URL=git@github.com:yourfork/IrredenEngine.git \
#   IR_MSYS2_MINGW_DIR=/c/msys2/mingw64/bin \
#   FLEET_CLONE=/c/work/IrredenEngine FLEET_CPU_BUDGET=32 bash setup-windows.sh
#
# Source of truth: scripts/fleet/setup-windows.sh in the engine repo.

set -euo pipefail

FLEET_CLONE="${FLEET_CLONE:-$HOME/src/IrredenEngine}"
REPO_URL="${FLEET_REPO_URL:-git@github.com:jakildev/IrredenEngine.git}"
# CPU budget for builds. "auto" = all logical cores (nproc). This host runs the
# Windows fleet alone (not co-running with the WSL fleet), so use the whole box.
# Set a number to cap it if that ever changes (e.g. FLEET_CPU_BUDGET=16).
FLEET_CPU_BUDGET="${FLEET_CPU_BUDGET:-auto}"
FLEET_BUILD_WORKERS="${FLEET_BUILD_WORKERS:-4}"
# Engine fleet worktrees fleet-up drives (kept in sync with fleet-up's
# worktree list): the generic pane pool (pool-1..pool-9, any transient role)
# plus the pinned architect and queue-manager scratch worktrees.
# Game-repo worktrees are created by fleet-up itself when creations/game exists.
# STORAGE: each worktree's source is ~250 MB; once it compiles, its build/ tree
# is ~4-5 GB — the full list below grows to ~25-55 GB of build trees over time.
# On a tight disk, trim it: FLEET_ROLES="pool-1 pool-2".
FLEET_ROLES="${FLEET_ROLES:-opus-architect pool-1 pool-2 pool-3 pool-4 pool-5 pool-6 pool-7 pool-8 pool-9 queue-manager queue-manager-ingest}"

say() { printf '\n== %s ==\n' "$*"; }

# --- 1. Prerequisites -------------------------------------------------------
say "Checking prerequisites"
missing=0
for tool in git tmux jq claude; do
    if command -v "$tool" >/dev/null 2>&1; then
        echo "  ok: $tool ($(command -v "$tool"))"
    else
        echo "  MISSING: $tool — install with: pacman -S $tool   (claude: its own installer)" >&2
        missing=1
    fi
done
if [[ ! -x "${IR_MSYS2_MINGW_DIR:-/c/msys64/mingw64/bin}/g++.exe" ]] \
    && [[ ! -x /c/msys64/mingw64/bin/c++.exe ]]; then
    echo "  WARN: MSYS2 mingw64 toolchain not found at /c/msys64/mingw64/bin" >&2
    echo "        install with: pacman -S mingw-w64-x86_64-toolchain" >&2
fi
# ruff lints the fleet Python surface (scripts/fleet/) — run before committing
# fleet-script changes. Soft dependency (needed for the local Python lint, not
# to run the fleet), so install best-effort: MSYS2 package first, else pipx/pip.
if command -v ruff >/dev/null 2>&1; then
    echo "  ok: ruff ($(command -v ruff))"
elif pacman -S --needed --noconfirm mingw-w64-x86_64-ruff >/dev/null 2>&1; then
    echo "  ok: installed ruff via pacman"
elif command -v pipx >/dev/null 2>&1 && pipx install ruff >/dev/null 2>&1; then
    echo "  ok: installed ruff via pipx"
else
    echo "  WARN: ruff not installed — 'ruff check scripts/fleet/' (the fleet" >&2
    echo "        Python lint) won't run locally. Try: pacman -S mingw-w64-x86_64-ruff" >&2
fi
case "$(uname -s)" in
    MINGW*|MSYS*) : ;;
    *) echo "  ERROR: not a Windows MSYS2/Git-Bash shell (uname=$(uname -s))." >&2; exit 1 ;;
esac
(( missing == 0 )) || { echo "Install the missing tools above, then re-run." >&2; exit 1; }

# --- 2. Dedicated fleet clone ----------------------------------------------
say "Fleet clone at $FLEET_CLONE"
if [[ -d "$FLEET_CLONE/.git" ]]; then
    echo "  exists — fetching origin"
    git -C "$FLEET_CLONE" fetch origin --quiet
else
    echo "  cloning $REPO_URL"
    mkdir -p "$(dirname "$FLEET_CLONE")"
    git clone "$REPO_URL" "$FLEET_CLONE"
fi

# --- 3. Config files --------------------------------------------------------
say "Writing ~/.fleet/fleet-up.conf and ~/.config/irreden/host.toml"
mkdir -p "$HOME/.fleet" "$HOME/.config/irreden"
cat > "$HOME/.fleet/fleet-up.conf" <<EOF
# native-Windows fleet host — generated by setup-windows.sh (edit freely).
FLEET_ENGINE_ROOT="$FLEET_CLONE"
FLEET_BUILD_WORKERS=$FLEET_BUILD_WORKERS
EOF
cat > "$HOME/.config/irreden/host.toml" <<EOF
# native-Windows fleet host CPU/build coordination — generated by setup-windows.sh.
# budget "auto" = all logical cores (nproc); this host runs the fleet alone.
[cpu]
budget = $FLEET_CPU_BUDGET

[concurrency]
workers = $FLEET_BUILD_WORKERS
queue_timeout_seconds = 600

[fleet]
# Centralized cross-device polling (#1394 Q2). "leader" polls GitHub and serves
# its ~/.fleet/state bundle to followers on the LAN; "follower" consumes the
# leader's bundle over the LAN instead of calling gh (set leader_host to the
# leader's LAN address/hostname). Exactly ONE host per GitHub account should be
# the leader; a single-host fleet is a leader. The leader opens one inbound TCP
# port (poll_port) on the LAN.
poll_role = "leader"
poll_port = 8477
# poll_role = "follower"
# leader_host = "192.168.1.10"   # required on a follower
EOF
if [[ "$FLEET_CPU_BUDGET" == "auto" ]]; then
    echo "  cpu budget=auto (nproc=$(nproc))  workers=$FLEET_BUILD_WORKERS  (per_build_max=$(( $(nproc) / FLEET_BUILD_WORKERS )))"
else
    echo "  cpu budget=$FLEET_CPU_BUDGET  workers=$FLEET_BUILD_WORKERS  (per_build_max=$(( FLEET_CPU_BUDGET / FLEET_BUILD_WORKERS )))"
fi

# --- 4. PATH (idempotent ~/.bashrc block) ----------------------------------
say "Putting fleet tool dirs on PATH (~/.bashrc)"
marker="# IRREDEN_ENGINE: fleet tool dirs"
# Idempotent + repointing: drop any prior marked block (marker line + its
# following export), then append a fresh one targeting this FLEET_CLONE — so a
# re-run with a different FLEET_CLONE moves PATH instead of stacking entries.
if [[ -f "$HOME/.bashrc" ]] && grep -qF "$marker" "$HOME/.bashrc"; then
    sed -i "\\|$marker|,+2d" "$HOME/.bashrc"
    echo "  removed previous fleet PATH block"
fi
cat >> "$HOME/.bashrc" <<EOF
$marker (setup-windows.sh; no Developer-Mode symlinks needed)
export PATH="$FLEET_CLONE/scripts/fleet:$FLEET_CLONE/engine/tools/bin:\$PATH"

EOF
echo "  set fleet tool dirs on PATH — run 'source ~/.bashrc' or open a new shell"

# --- 5. Fleet worktrees -----------------------------------------------------
# fleet-up creates/repairs these itself, but pre-creating them off origin/master
# makes the first launch fast and lets a build smoke-test run before going live.
say "Creating fleet worktrees off origin/master"
git -C "$FLEET_CLONE" fetch origin master --quiet
for role in $FLEET_ROLES; do
    wt="$FLEET_CLONE/.claude/worktrees/$role"
    branch="claude/$role-scratch"
    if git -C "$FLEET_CLONE" worktree list --porcelain | grep -qF "worktree $wt"; then
        echo "  exists: $role"
        continue
    fi
    if git -C "$FLEET_CLONE" worktree add --force -B "$branch" "$wt" origin/master >/dev/null 2>&1; then
        echo "  created: $role -> $branch"
    else
        echo "  WARN: could not create worktree for $role (fleet-up will create it on launch)" >&2
    fi
done

say "Done"
cat <<EOF
Next:
  1. source ~/.bashrc            # pick up the fleet tool dirs on PATH
  2. cd "$FLEET_CLONE"
  3. fleet-up dry-run            # creates the tmux 'fleet' session (agents stand by)
  4. tmux attach -t fleet        # watch; type to take a pane out of dry-run
  5. fleet-up live               # when ready for the real loop
Tear down any time with: fleet-down
EOF
