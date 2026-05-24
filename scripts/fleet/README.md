# scripts/fleet/

Launcher and per-machine installer for the Irreden Engine parallel-agent
fleet workflow.

## Files

- **`fleet-up`** — brings the 8-pane tmux fleet online. Idempotent.
  Creates any missing worktrees, resets each to a fresh branch off
  `origin/master`, builds a `fleet` tmux session with one tiled
  window, and auto-launches `claude` in each pane with the matching
  role slash command. Default mode is `dry-run` (startup + stand-by);
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
- **`fleet-help`** — prints an index of all `fleet-*` tools (build, run,
  tmux fleet, claims, …) and how to install them; `fleet-help <cmd>`
  forwards to `--help` when the tool supports it (or a short summary).
- **`completions/fleet-run.bash`** — bash tab completion for `fleet-run`,
  `fleet-build`, `fleet-debug`, `ir-run`, and `ir-build`. The `fleet-*`
  and `ir-*` counterparts share the same completion logic: `ir-run`
  completes built exe names on the first positional; `ir-build` completes
  CMake demo names after `--target`. Target discovery uses
  `fleet-run-targets`, which resolves the invoker's git worktree root, so
  game-repo invocations show game targets. `install.sh` symlinks it into
  `${XDG_DATA_HOME:-~/.local/share}/bash-completion/completions/` for
  bash-completion / Homebrew `bash-completion@2`.
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
