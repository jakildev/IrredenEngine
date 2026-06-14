# Plan: fleet — scout/ingest/fleet-claim read the stale main-clone working tree

- **Issue:** #1810
- **Model:** opus
- **Date:** 2026-06-14
- **Area:** scripts/fleet (one task; no stack)

## Root cause (verified this iteration, mac host)

The `~/bin/fleet-*` symlinks (`fleet-state-scout`, `fleet-claim`,
`fleet-queue-ingest`, `fleet-dispatcher`, `fleet-up`) all resolve to the
**main clone working tree** `~/src/IrredenEngine/scripts/fleet/`. scout and
fleet-claim import the `blocked_by` parser from `FLEET_LIB_DIR` = the script's
own (resolved-symlink) directory = that same tree
(`fleet-state-scout:37,40`; `fleet-claim:110,119,507,513`). When the main
clone's checked-out `master` lags `origin/master`, the scripts **and** their
python modules (`fleet_blocked_by.py`, added by merged #1783) are stale or
physically absent — so the merged parser fix never runs.

Decisive refinement: issue **bodies are read live via `gh issue view --json
body`** (`fleet-claim:452,3549`; scout fetches via gh) — the *data* is already
fresh. **Only the parser/script CODE is stale.** => the issue body's option
(b) ("read bodies from origin/master refs") is a misdiagnosis; the fix must
refresh the **code**, i.e. advance the clone.

Why it persists: `fleet-up` runs `git -C $ENGINE fetch origin` (`fleet-up:435`)
and resets the **worktrees** to origin/master (`fleet-up:717-719`), but
**never fast-forwards the main clone's own checked-out `master`**. Nothing
else does either (`grep -rE 'pull --ff-only|merge --ff-only' scripts/fleet`
→ none). So the clone stays pinned wherever last manually pulled (observed
`22102ab6`, 20 behind) and re-drifts as new PRs merge mid-session. The scout's
own note (`fleet-state-scout:1068`) forbids per-tick fetches and assumes a
"normalize bootstrap" pull that, for the main clone, **does not exist** — that
is the gap.

## Approach (one opus task)

A guarded, rate-limited fast-forward of the main clone + a fail-loud freshness
assertion on the claim side + a fetch-free freshness annotation in state.json.

1. **`advance_main_clone` guarded helper** — new sourced bash helper
   `scripts/fleet/fleet-clone-freshness.sh` (sourced by-dir, the same way
   `fleet-common.sh` resolves through a ~/bin symlink). `advance_main_clone <repo_root>`:
   - rate-limit: skip the fetch if `~/.fleet/state/.<repo>-advanced` sentinel
     is < 60s old; else `git -C <root> fetch origin master --quiet` + touch it.
   - guard (ALL required): clone is (a) on branch `master`,
     (b) `git status --porcelain` empty, (c) `master` strictly behind AND a
     pure ancestor of `origin/master` (`merge-base --is-ancestor master
     origin/master` true and heads differ). Then `git merge --ff-only
     origin/master` — never a checkout-switch, never `reset --hard`.
   - on any guard miss (off-master / dirty / diverged): skip + loud `>&2`
     warning, do NOT mutate. Mirrors the `reset_worktree` dirty-guard idiom
     (`fleet-up:707-714`).
   - Wire into **fleet-up startup** (right after the `$ENGINE`/`$GAME` fetches
     at :435 / :466) AND the **dispatcher main loop** (once per pass — this is
     the home that fixes mid-session re-drift; fleet-up alone is insufficient
     because the issue observed drift across multiple scout cycles in one
     session).
2. **fleet-claim freshness guard (fail-loud)** — in the blocker-gate path
   (around the `parse_blocked_by` import at `fleet-claim:507-521`), before
   trusting the parser, compare `git -C $ENGINE rev-parse master` vs
   `rev-parse origin/master` (**no fetch** — uses the ref fleet-up/dispatcher
   already fetched). If `master` is strictly behind origin/master, **refuse
   the claim** with `fleet-claim: main clone is N commits behind origin/master
   (stale parser) — run 'git -C ~/src/IrredenEngine merge --ff-only
   origin/master' or fleet-up` and exit non-zero. Converts the silent
   false-grant into a loud refusal (the "fail loud instead of false-free"
   requirement).
3. **state.json freshness annotation (scout)** — scout emits
   `clone_freshness: {head, origin_master_head, behind: N, fresh: bool}` using
   **rev-parse only, no fetch** (respects the no-fetch-in-hot-path rule). Lets
   readers/dispatcher see staleness; dispatcher logs a warning when
   `fresh==false` persists.
4. **Scout module-resolution fix — HARD PREREQUISITE (the #1578 miss).**
   `fleet-state-scout:37` resolves its lib dir with
   `os.path.dirname(os.path.abspath(__file__))` — `abspath` does **not**
   dereference the `~/bin` symlink, so it yields `~/bin`, where
   `fleet_blocked_by.py` is **absent** (`install.sh:182` symlinks the
   executables only, not the `.py` helpers). The currently-running scout
   survives solely because it predates #1783 (parser inlined, no import); the
   moment the clone advances to the merged scout, `from fleet_blocked_by
   import …` (`fleet-state-scout:40`) raises `ModuleNotFoundError` and the
   daemon dies. Fix: change `:37` to `os.path.realpath(__file__)` — the same
   symlink-deref #1578 applied to `fleet-claim:105-111` and
   `fleet-dispatcher:74-78` but **missed** for the scout — so the lib dir
   resolves to the main-clone `scripts/fleet/` where the module lives. This
   step must land WITH the clone-advance, not after: advancing without it
   trades a stale parser for a dead scout.
5. **Long-running scout daemon must re-exec to pick up advanced code.** The
   scout `import`s the parser **once at module load**; fast-forwarding the
   clone on disk does not reload an already-running daemon, so the periodic
   advance fixes the *files* while the live scout keeps projecting from stale
   in-memory parser code until something restarts it — the mid-session
   re-drift the issue observed. Fix: when the scout's own freshness rev-parse
   (step 3) detects the clone advanced, re-exec **between ticks**:
   `os.execv(sys.executable, [sys.executable, os.path.realpath(__file__)] +
   sys.argv[1:])`. Same PID (so `scout.pid` stays valid); state.json is
   written atomically per tick (`write_atomic`), so there is no torn state.
   Use `realpath` (not `abspath`) so the re-exec targets the now-updated
   main-clone file.

## Affected files
- `scripts/fleet/fleet-clone-freshness.sh` — NEW guarded `advance_main_clone` + `assert_clone_fresh`.
- `scripts/fleet/fleet-up` — source helper; advance after the engine/game fetches (:435/:466); refresh the clone once before `nohup`-ing the scout so the daemon starts on fresh code (step 5).
- `scripts/fleet/fleet-dispatcher` — source helper; periodic advance in the main loop.
- `scripts/fleet/fleet-claim` — fail-loud freshness guard in the blocker-gate path (~:507-521).
- `scripts/fleet/fleet-state-scout` — emit `clone_freshness` (rev-parse only); **fix `:37` `abspath`→`realpath`** (step 4, the #1578 miss); **re-exec via `os.execv` on detected advance** (step 5).
- `scripts/fleet/install.sh` — symlink the new helper into ~/bin (by-dir source resolves through the symlink, like fleet-common.sh).

## Acceptance criteria
- After a fleet-script fix merges to origin/master, the next dispatcher pass
  (or fleet-up) advances the main clone: `git -C ~/src/IrredenEngine
  merge-base --is-ancestor <fix-sha> master` → true with no manual pull.
- A genuinely-blocked task with a non-bold `Blocked by: #N` line (e.g. #174
  Phase-E children) projects `blocked=true`, and `fleet-claim` refuses the
  claim — because the parser is now fresh, or, if somehow still stale, because
  the fail-loud guard fires.
- Guard safety: off-master / dirty / diverged main clone → advance skipped
  with a warning, working tree untouched (no clobber of a checked-out branch).
- state.json carries `clone_freshness.fresh`.
- Clone-advance does not kill the scout: after `merge --ff-only` brings in a
  scout that imports `fleet_blocked_by`, the daemon imports cleanly
  (`fleet-state-scout:37` resolves to `scripts/fleet/` via `realpath`) — no
  `ModuleNotFoundError`.
- Mid-session reload: when the clone advances while the scout daemon is
  running, the daemon re-execs (same PID) and its next state.json projection
  reflects the now-fresh parser without a manual fleet-up restart.

## Gotchas
- **Module-resolution fragility (#1750/#1578):** step 4 fixes the underlying
  `abspath` bug so the **existing** `fleet_blocked_by` import resolves; even
  so, do NOT add a *new* scout-imported python module — keep the scout's
  freshness read as a tiny inline `git rev-parse` (≤5 lines), not an import.
  Bash consumers share `fleet-clone-freshness.sh` (sourced by-dir).
- **install.sh must symlink the new helper** into ~/bin — all fleet scripts
  are individually symlinked; a sibling sourced via `dirname` only resolves if
  it is symlinked there too (see the fleet-common.sh header).
- **Bootstrap chicken-and-egg:** the fix lives in scripts that themselves run
  from the stale clone, so deploying it needs ONE manual `git -C
  ~/src/IrredenEngine merge --ff-only origin/master` (or fleet-up restart).
  Self-healing thereafter — call this out in the PR body.
- **Shared-checkout hazard** (`feedback_main_worktree_shared`): the main clone
  is shared; agents may rarely check out a branch there. The
  off-master/dirty/diverged guard is mandatory — only `merge --ff-only`, only
  when clean + on-master + pure-ancestor.
- **No per-tick fetches in the scout** (`fleet-state-scout:1068`): the advance
  lives in fleet-up + dispatcher (periodic), never the scout cache regen; the
  scout does fetch-free rev-parse only.

## Cross-system audit (consumers of the stale-clone mechanism)
All run from the main clone via ~/bin symlinks and depend on fresh parser code:
- `fleet-state-scout` — blocked/owner/stackable projection (parser import :40).
- `fleet-claim` — blocker-gate false-grant (parser import :513).
- `fleet-queue-ingest` — fleet:queued stamping gate (same parser family).
- `fleet-dispatcher` — consumes the scout projection; new home for the periodic advance.
All are fixed transitively by advancing the clone; fleet-claim additionally
gets the explicit fail-loud guard as defense-in-depth.

## Sibling / in-flight reconciliation
- #1749 / #1783 (blocked_by parser, MERGED) — this is the **deployment**
  complement, not a duplicate; the parser is correct but inert.
- #1750 (state.json atomicity) and #1751 (stackable base guard) — distinct
  families per the issue body; no surface overlap.
- No open PR touches the fleet-up / dispatcher / fleet-claim clone-advance
  surface (engine PRs #1742, #1811 are render-only).

## Reconciliation note (2026-06-14)

This issue was planned **three times concurrently** in a claim race (#1821
merged as this plan-of-record; #1822 and #1825 closed as superseded) — a live
instance of the stale-clone false-grant this very plan fixes (the planning
gate fired 3× within seconds because the claim lock wasn't observed freshly).

Steps 4–5, their matching affected-files / acceptance entries, and this
correction were folded in from the closed #1825 plan + worker-3's #1810
addendum, which caught two deploy-blockers the originally-merged plan omitted:

- **Root-cause correction:** the "Root cause" section above states scout
  imports from "the script's own (resolved-symlink) directory." That holds for
  `fleet-claim` (post-#1578) but **NOT** for the scout — `fleet-state-scout:37`
  uses `abspath`, which does not deref the symlink. The scout is precisely the
  spot #1578 missed; step 4 is the fix.
- Without steps 4–5, advancing the clone trades a stale parser for a *crashed*
  (ModuleNotFoundError) or *stale-in-memory* scout daemon — i.e. the fix as
  originally written would not actually deploy.
