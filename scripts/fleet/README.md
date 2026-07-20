# scripts/fleet/

Launcher and per-machine installer for the Irreden Engine parallel-agent
fleet workflow.

## Files

- **`fleet-up`** — brings the tmux fleet online. Idempotent.
  Creates any missing worktrees (the shared `pool-1`…`pool-9` pool
  worktrees plus the named architect worktrees), resets each to a
  fresh branch off
  `origin/master`, and builds a `fleet` tmux session with a fleet
  window (pool panes 1–6 + the architect panes) and an ops window
  (queue-manager scratch, pool panes 7–9, witness, terminal). The
  architect panes auto-launch `claude`; pool panes sit at bash and
  `fleet-dispatcher` routes transient roles (worker, reviewers,
  merger, smoke-worker, epic-steward) into whichever pool pane is
  idle, bounded by per-role `FLEET_CONCURRENCY_<ROLE>` caps. Default mode is `dry-run` (startup + stand-by);
  `fleet-up live` skips dry-run, goes straight to the normal loop, and
  auto-attaches the tmux session. `fleet-up review-only` runs the
  loop but tells worker / queue-manager roles to skip new task
  pickup and new issue ingestion — reviewers, mergers, smoke
  validators, semantic-conflict resolvers, feedback handlers, and
  in-flight molecule continuation all still run, so it's the right
  mode for closing out open PRs without expanding the queue (e.g.
  conserving credits before a session boundary). Pass `--no-attach`
  to opt out of the attach (CI / headless runs).
- **`fleet-down`** — graceful shutdown of the fleet. Sends Ctrl-C +
  "exit" to each pane, waits a short grace period, then kills the
  tmux session and clears stale fleet-claim locks. `--force` skips
  the graceful step; `--keep-claims` preserves `~/.fleet/claims`.
- **`solo-architect`** — the inverse of `fleet-up`: launches a single
  architect interactively (`solo-architect` for the engine
  `/role-opus-architect`, `solo-architect game` for
  `/role-game-architect`) WITHOUT spawning any workers, reviewers,
  merger, or scout daemon. Same model / effort / worktree / persisted-
  session resume as the fleet's architect pane, so you get the full
  design-partner + sub-agent-direction role as a one-off conversation.
  Primes the scout cache with one `fleet-state-scout --once` tick (skip
  with `--no-scout`) and appends a solo-mode prompt so the role doesn't
  exit on a stale/missing cache. `--fresh` starts a new conversation;
  `--dry-run` does startup + stand-by only. Tasks it files just wait in
  the queue until you bring the fleet up.
- **`install.sh`** — one-time setup per machine. Symlinks every
  `scripts/fleet/fleet-*` script into `~/bin/` and symlinks each
  `.claude/commands/role-*.md` into `~/.claude/commands/`. Picks up
  the game-architect role from `creations/game/` if that repo is
  cloned. Installs shell completions; for zsh, **idempotently appends**
  a marked two-line block to `~/.zshrc` that sources
  `~/.zsh/completions/irreden-fleet.zsh` (skip duplicate if the marker
  is already there). Use **`--no-zshrc`** or **`IRREDEN_INSTALL_SKIP_ZSHRC=1`**
  to only symlink completions without editing `~/.zshrc` (e.g. CI).
  Run **`install.sh --help`** for options. Warns (but does not edit PATH) if `~/bin` is
  not on your PATH. **Ubuntu / WSL:** same script; bash completion uses
  `~/.local/share/bash-completion/completions/` (needs the
  `bash-completion` package and the usual `~/.bashrc` hook). The
  `~/.zshrc` snippet is added only when you already use zsh (or macOS);
  pure-bash setups are not given a new `~/.zshrc`.
  Idempotent — re-run after every `git pull` that
  touches fleet tooling.
- **`fleet-run --targets`** (also the `fleet-run-targets` script in this
  directory) — lists names you can pass to `fleet-run`: built executables
  under `creations/` and `test/` by default, or `--plan` for CMake
  demo/test target names from `cmake --build --target help`. Use
  `fleet-run --targets --plain` for one name per line (e.g. pipe to `fzf`).
- **`fleet-debug`** — builds a target through `fleet-build`, finds the
  resulting executable under `build/`, changes to its runtime directory,
  and launches it under `lldb` or `gdb`. Use `--batch` for non-interactive
  crash triage that runs once and prints all thread backtraces.
- **`fleet-edit`** — exact string replacement CLI (Python3). Takes a
  target file, an old-text file, and a new-text file; replaces exactly
  one occurrence (or all with `--replace-all`). Use for tasks that need
  to modify documentation or configuration files where the built-in
  `Edit` tool is unavailable.
- **`fleet-help`** — prints an index of all `fleet-*` tools (build, run,
  tmux fleet, claims, …) and how to install them; `fleet-help <cmd>`
  forwards to `--help` when the tool supports it (or a short summary).
- **`completions/fleet-run.bash`** — bash tab completion for `fleet-run`
  and `ir-run` (built exe names when the word does not start with `-`),
  `fleet-build` and `ir-build` (CMake demo names after `--target`), plus
  `fleet-debug`. `install.sh` symlinks it into
  `${XDG_DATA_HOME:-~/.local/share}/bash-completion/completions/`
  for bash-completion / Homebrew `bash-completion@2`.
- **`completions/irreden-fleet.zsh`** — zsh entry: ensures `compinit` (if
  needed), runs `bashcompinit`, then sources `fleet-run.bash`. **macOS
  default shell is zsh**; the bash-completion directory is not used.
  `install.sh` symlinks this and `fleet-run.bash` into `~/.zsh/completions/`
  and may append a marked `source` line to `~/.zshrc` when zsh is likely in
  use (see `install.sh`); safe to re-run.

All files are portable across Linux (WSL/Ubuntu) and macOS. None
requires sudo.

## Quick start

```bash
cd ~/src/IrredenEngine
scripts/fleet/install.sh
# (follow the PATH warning if ~/bin isn't on PATH yet)
fleet-up dry-run                # dry run — confirms panes spawn cleanly
# ...inspect, then promote each pane manually, or:
fleet-down                      # shut down before going live
fleet-up live                   # full loop, auto-attaches
# Ctrl-a d to detach without killing the fleet.
fleet-down                      # graceful shutdown when done for the day.
```

## Updating the fleet across machines

Because `install.sh` uses symlinks, editing `scripts/fleet/fleet-up`
in the repo and committing it is enough — every machine that has run
`install.sh` sees the new version on its next `git pull`. Same story
for the role slash commands under `.claude/commands/role-*.md`.

The only time you need to re-run `install.sh` is when a new file is
added (or a file is deleted) — `git pull` alone doesn't create new
symlinks or prune stale ones.

## Full docs

See [`docs/AGENT_FLEET_SETUP.md`](../../docs/AGENT_FLEET_SETUP.md) for
the complete setup guide: worktree layout, role-to-model split,
`~/.tmux.conf` template, daily ritual, dry-run walkthrough, permission
allowlists, and troubleshooting.
