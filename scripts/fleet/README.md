# scripts/fleet/

Launcher and per-machine installer for the Irreden Engine parallel-agent
fleet workflow.

## Files

- **`fleet-up`** — brings the 7-pane tmux fleet online. Idempotent.
  Creates any missing worktrees, resets each to a fresh branch off
  `origin/master`, builds a `fleet` tmux session with one tiled
  window, and auto-launches `claude` in each pane with the matching
  role slash command. Default mode is `dry-run` (startup + stand-by);
  pass `live` to skip dry-run and go straight to the normal loop.
- **`install.sh`** — one-time setup per machine. Symlinks
  `scripts/fleet/fleet-up` into `~/bin/fleet-up` and symlinks each
  `.claude/commands/role-*.md` into `~/.claude/commands/`. Picks up
  the game-architect role from `creations/game/` if that repo is
  cloned. Warns (but does not edit) if `~/bin` is not on PATH.
  Idempotent — re-run after every `git pull` that touches fleet tooling.

Both files are portable across Linux (WSL/Ubuntu) and macOS. Neither
requires sudo.

## Quick start

```bash
cd ~/src/IrredenEngine
scripts/fleet/install.sh
# (follow the PATH warning if ~/bin isn't on PATH yet)
fleet-up dry-run
tmux attach -t fleet
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
