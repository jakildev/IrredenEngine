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
  cloned. Warns (but does not edit) if `~/bin` is not on PATH.
  Idempotent — re-run after every `git pull` that touches fleet tooling.

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
