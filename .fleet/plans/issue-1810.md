# Plan: fleet: scout/ingest/fleet-claim read the stale main-clone working tree — merged fleet-script fixes stay inert

- **Issue:** #1810
- **Model:** opus
- **Date:** 2026-06-14

## Scope

`fleet-state-scout`, `fleet-queue-ingest`, and `fleet-claim` all run their
Python/bash from the **main clone working tree** at `~/src/IrredenEngine`
(via `~/bin/*` symlinks that `install.sh` deliberately points at the main
clone, never a worktree). That tree is fast-forwarded **once** by `fleet-up`
at bootstrap (`fleet-up:435` `git fetch`, no pull) and then never advanced,
so a fleet-script fix that merges to `origin/master` mid-session never runs.
Observed chronic: the #1783 `blocked_by` parser (`fleet_blocked_by.py`)
merged but was physically **absent** from the clone for >1 day / 20 commits,
so genuinely-blocked tasks with a non-bold `Blocked by: #N` line (game
#178/#179) projected `blocked=false`/`owner=free` and `fleet-claim` would
false-grant.

Make the scout keep the clone current and **fail loud** when it can't, so
all three consumers run merged code. This is the human's directed path
("`git pull --ff-only` before each scout run … add a freshness assertion so
a stale clone fails loud instead of silently false-freeing", #1810 comment
2026-06-14T22:43:22Z), made correct for the long-lived daemon.

## Verified current state

- **Data is already fresh.** All three consumers read issue/PR state from
  the `gh` API, not the working tree. The staleness is purely the **script
  code** they execute (e.g. `fleet_blocked_by.py`), not the data they parse.
- **Scout is a long-lived daemon.** `fleet-state-scout` `main()` loops
  `while not TERMINATE: tick_once(); sleep(interval)` (`fleet-state-scout`
  ~2178-2188) and imports `fleet_blocked_by` / `fleet_stack_base` /
  `fleet_branch_match` **once** at module load (`:37-40`). A bare per-cycle
  `git pull` would update the working tree but **not** reload the running
  process's already-imported parser — the daemon must **re-exec** to pick up
  a changed script. This is the crux the naive "ff-pull" framing misses.
- **The other two consumers are short-lived** and pick up fresh code for
  free once the clone advances: `fleet-queue-ingest` is spawned as a fresh
  subprocess by the scout each projection change; `fleet-claim` is a fresh
  bash process per invocation (re-resolves `FLEET_LIB_DIR` and re-imports
  every call, `fleet-claim:108-126`). So **freshening the engine clone is
  the single fix point**; only the scout needs the re-exec.
- **Shared-checkout hazard is real.** The main clone is occasionally left on
  a branch or dirty by an agent (the "main checkout is shared" hazard noted
  in the issue body), so the ff must be **guarded**, never forced.
- All fleet scripts live in the **engine** clone's `scripts/fleet/`; game-side
  parsing uses the same engine modules. So only the engine clone needs
  freshening (the game clone holds no fleet scripts).

## Approach (one approach, picked)

Add a guarded freshen-and-self-restart step to the scout's poll loop, plus a
cheap no-network staleness guard in `fleet-claim`. All in `scripts/fleet/`
(no role/skill/agent self-config — worker-editable).

1. **`scripts/fleet/fleet-state-scout` — new `freshen_main_clone()` helper,
   called at the TOP of each loop iteration, before `tick_once()`** (so a
   re-exec never lands mid-tick):
   - `git -C ENGINE fetch origin --quiet` (cheap; refs only). On failure,
     log and return (run with whatever's on disk; do not crash the daemon).
   - Read `HEAD` (`git rev-parse HEAD`) and `origin/master`
     (`git rev-parse origin/master`). If equal → return (already current).
   - **Guard — only fast-forward when ALL hold:**
     - on `master`: `git symbolic-ref --quiet --short HEAD` == `master`
       (skip when detached or on a feature branch),
     - clean tree: `git status --porcelain` empty (don't clobber edits),
     - true fast-forward: `git merge-base --is-ancestor HEAD origin/master`.
   - If guard fails → **fail loud**: `log()` a `WARNING: main clone stale —
     HEAD <sha> is <N> behind origin/master <sha>; fleet scripts may be
     inert (reason: off-master/dirty/diverged)`, set the freshness flag in
     state (step 3), and return without touching the tree.
   - If guard passes → `git merge --ff-only origin/master`. Capture
     `old_head`→`new_head`.
   - **Re-exec on script change:** if `git diff --name-only old_head new_head
     -- scripts/fleet/` is non-empty, the running daemon's imported code is
     now stale → `log("scripts/fleet changed; re-exec for fresh code")`,
     `sys.stdout/err.flush()`, then
     `os.execv(sys.executable, [sys.executable, os.path.abspath(__file__),
     *sys.argv[1:]])`. `execv` preserves the PID, so `scout.pid` and the
     nohup parent stay valid; the new image re-imports from the updated disk,
     re-enters `main()`, rewrites the same PID, resumes the loop. No re-exec
     loop: after restart `HEAD == origin/master`, so the next fetch finds no
     advance.

2. **`fleet-claim` — cheap fail-loud staleness guard** (no network; uses the
   already-fetched local `origin/master` ref the scout keeps current). After
   the existing `FLEET_LIB_DIR` module-existence check (`fleet-claim:119-126`),
   add: if `git -C "$FLEET_LIB_DIR/../.." merge-base --is-ancestor
   origin/master HEAD` is **false** (i.e. the clone's HEAD is behind
   origin/master), emit a `fleet-claim: WARNING — running scripts <N> behind
   origin/master; blocker/claim decisions may use a stale parser` to stderr.
   Warn-only (don't refuse — a refuse would wedge claims if the scout is down);
   the loud signal is what the acceptance criterion asks for on the claim side.

3. **Surface freshness in `state.json`** so workers can see staleness even
   when they don't read the scout log. In `tick_once()` write a top-level
   `clone_freshness: {head, origin_master, behind_count, stale: bool,
   reason: str|null}` object (additive field; existing consumers ignore
   unknown keys). `freshen_main_clone()` sets `_clone_stale_reason` (module
   global) that `tick_once()` reads when assembling state. role-worker can
   later surface this in its startup banner, but that's out of scope here.

## Affected files

- `scripts/fleet/fleet-state-scout` — add `freshen_main_clone()`; call it at
  the top of the `while` loop in `main()`; add `clone_freshness` to the state
  dict in `tick_once()` / `collect_state()`.
- `scripts/fleet/fleet-claim` — add the cheap no-network staleness warning
  after the existing lib-dir check.
- `.fleet/plans/issue-1810.md` — this plan (already staged).

## Acceptance criteria

- After a `scripts/fleet/` fix merges to `origin/master`, the **next scout
  tick** fetches, guard-passes, fast-forwards, and (because a fleet script
  changed) re-execs — so the running scout then runs the merged code, with
  **no manual pull**. Verify on the running clone:
  `git -C ~/src/IrredenEngine merge-base --is-ancestor <fix-sha> HEAD` → true.
- A genuinely-blocked task with a **non-bold** `Blocked by: #N` line (the
  #174 Phase-E children, e.g. game #179 blocked by OPEN #178) projects
  `blocked=true`/`blocked_by=#178` and `fleet-claim … claim 179` refuses.
- When the clone is **off-master / dirty / diverged**, the scout logs a loud
  `WARNING` and `state.json.clone_freshness.stale == true` with a reason —
  it never silently false-frees.
- `fleet-claim` prints a stderr staleness warning when invoked from a clone
  whose HEAD is behind `origin/master`.

## Gotchas

- **Re-exec must be at the top of the loop, before `tick_once()`** — never
  mid-tick (would abandon a partial `write_atomic`/projection pass).
- **`os.execv` preserves the PID** → `scout.pid` (`~/.fleet/state/scout.pid`)
  and `fleet-down`'s kill-by-pid both keep working. Verify `fleet-down` after.
- **Guard is mandatory.** A forced ff/`reset --hard` on the shared clone
  could clobber an agent's in-progress edits or a checked-out branch. Skip +
  warn is the correct failure, not a hard reset.
- **No re-exec loop** — gate the `os.execv` on `old_head != new_head` AND a
  non-empty `scripts/fleet/` diff; after restart HEAD equals origin/master so
  the next cycle is a no-op.
- **`fleet-claim` guard is warn-only** — refusing on staleness would wedge
  all claims whenever the scout is down or the clone legitimately can't ff.
- `git diff --name-only` path must be `scripts/fleet/` (where every consumer's
  code + shared modules live); a fix elsewhere (e.g. `docs/`) shouldn't
  trigger a re-exec but a harmless extra restart is acceptable if scoping
  proves awkward.
- **Test with `--once`**: `freshen_main_clone()` should be reachable in
  `--once` mode for a unit test, but the re-exec branch should be guarded so
  `--once` doesn't loop (it returns after one tick anyway; just don't re-exec
  in `--once`, or accept that `--once` re-execs then runs one tick — prefer
  skipping re-exec when `args.once`).
