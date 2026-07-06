# Plan — Issue #2262: fleet/install-symlink-lag — mtime-guarded auto-refresh of install.sh

- **Issue:** #2262 (surfaced by `review-fleet-feedback`; signature `install-symlink-lag`, 7 occurrences)
- **Model:** opus
- **Blocked by:** (none)

## Scope

Fleet scripts / role slash-commands / `ir-*` tools that merge to master never
refresh a host's `~/bin` symlinks while a fleet run is live, so a freshly-added
tool is `command not found` on first use. `fleet-review-verdict` (merged
2026-07-03, PR #2193) stranded every reviewer on 2026-07-04 — 7 feedback
entries, each re-inventing the manual `install.sh --no-zshrc` re-run. Same
family as the `fleet_*.py` stale-symlink pattern.

Fix: an **mtime-guarded auto-refresh** of `install.sh` at two hook points —
(1) fleet-up bring-up (covers a fleet restart after a merge) and (2) the
per-pane dispatch wrapper (covers a tool merged *while* the fleet is already
running — the actual stranding case in the feedback).

Out of scope: changing what `install.sh` symlinks; the 7-day feedback
quiet-window verification (that's post-merge monitoring, not PR-gated).

## Approach

1. **Stamp file** `~/.fleet/state/.install-stamp` (empty touch-file).
   `install.sh` `touch`es it as its final successful action (right at the
   closing `echo "install.sh: done."`). Both manual and auto runs bump it, so
   the stamp always reflects the last real symlink pass.
2. **Shared staleness check** in `scripts/fleet/fleet-common.sh` (already
   symlinked into `~/bin` and available to fleet-up + the dispatch wrapper):
   `fleet_install_stale <main-clone-root>` returns 0 (stale) when the stamp is
   missing **or** any source is newer than it — resolved against the **main
   clone** (the symlink target the scout keeps fresh via `git pull`), using a
   short-circuiting `find "$dir" -newer "$stamp" -print -quit` over:
   - `scripts/fleet/` (tool sources)
   - `engine/tools/bin/ir-*` (ir-* tool sources — install.sh symlinks these)
   - `.claude/commands/role-*.md` + `creations/game/.claude/commands/role-*.md`
     (role slash-commands, install.sh Steps 2/3)
3. **fleet-up preflight** — new "Step 0.4" before Step 1: if
   `fleet_install_stale`, run `install.sh --no-zshrc` (it re-stamps). Gives a
   clean-symlink baseline to every pane fleet-up then launches.
4. **Per-pane refresh** in `scripts/fleet/fleet-dispatch-wrap` (the wrapper the
   dispatcher `tmux send-keys`-invokes per pane), before it starts the pane's
   `claude`: same `fleet_install_stale` check; on stale, run
   `install.sh --no-zshrc` under an **atomic mkdir-lock** (reuse the
   `_acquire`-style `mkdir`-as-lock primitive fleet-claim already uses — NOT
   `flock`, absent on macOS) so concurrent pane dispatches don't race the
   symlink writes + stamp. This is the hook that fixes the mid-run stranding.

## Acceptance

- After a new script lands in the main clone's `scripts/fleet/` (mtime newer
  than the stamp), the next pane dispatch symlinks it into `~/bin` before that
  pane's `claude` starts — no `command not found`.
- No newer-than-stamp source ⇒ zero re-link work: the `find` short-circuits and
  `install.sh` is not invoked (idempotent, sub-ms common path).
- Concurrent dispatches never corrupt `~/bin` or the stamp (mkdir-lock +
  `install.sh`'s idempotent `ln -sf`).
- New `scripts/fleet/tests/test_install_refresh.sh` (sibling of
  `test_clone_freshness.sh`): builds a temp repo + `HOME`, asserts
  (a) stamp-missing ⇒ stale; (b) touch a `scripts/fleet` file newer than the
  stamp ⇒ stale ⇒ refresh creates the symlink and bumps the stamp;
  (c) re-run with no newer file ⇒ not stale, `install.sh` not invoked.
- Post-merge monitoring: `review-fleet-feedback digest` stops re-surfacing the
  `install-symlink-lag` cluster across the 7-day quiet window.

## Affected files

- `scripts/fleet/install.sh` — `touch` the stamp as the final success step.
- `scripts/fleet/fleet-common.sh` — add `fleet_install_stale()` + a main-clone
  resolver (reuse install.sh's worktree→main-clone `git worktree list
  --porcelain` logic).
- `scripts/fleet/fleet-up` — Step 0.4 preflight refresh.
- `scripts/fleet/fleet-dispatch-wrap` — per-pane guarded refresh under the
  mkdir-lock (the primary fix).
- `scripts/fleet/tests/test_install_refresh.sh` — new test.
- **Not gated**: everything is under `scripts/fleet/` — the commit gate covers
  only `.claude/commands/role-*.md`, `.claude/agents/*`,
  `.claude/skills/**/SKILL.md`, so a worker can push the whole change.

## Gotchas

- **git-checkout mtime semantics**: `git pull` bumps mtime only on files it
  actually changes/adds — which is exactly the signal we want (a new/updated
  tool gets a fresh mtime; untouched files don't). Don't assume mtime is
  preserved across a pull.
- **macOS has no `flock`**: use the fleet-claim `mkdir`-as-lock primitive for
  the dispatch-wrap refresh lock, not `flock`.
- **Windows native-symlink failure path**: `install.sh` hard-exits when the
  MSYS2 shell can't create native symlinks. The auto-refresh must **not**
  re-run `install.sh` (and re-print that failure) on every dispatch on such a
  host — bump the stamp on an *attempted* refresh even when `install.sh` exits
  non-zero, so a symlink-incapable host degrades to one warning per
  new-tool-merge, not one per dispatch.
- **Main clone vs worktree**: the stale-check must resolve the **main clone**
  (the symlink target the scout pulls into), never the pane's worktree — reuse
  install.sh's main-clone resolution so a linked-worktree cwd doesn't compare
  against a stale/reset tree.
- **Latency**: the per-pane check is a `find -newer -print -quit` over ~3 small
  dirs — sub-ms in the common not-stale case; the expensive `install.sh` fires
  only on the rare stale hit.
