# Plan: scout/ingest/fleet-claim run stale fleet scripts from the un-fast-forwarded main clone

- **Issue:** #1810
- **Model:** opus
- **Date:** 2026-06-14
- **Area:** scripts/fleet (one task; no stack)

> This plan supersedes the earlier #1821 plan that merged to master first.
> It is the same deployment fix, kept as the canonical version because it
> verified the actual code paths (with line citations), picks a specific
> approach, and identifies a second root cause #1821 missed (the long-lived
> scout daemon's import caching). The #1821 plan's one genuinely-distinct
> contribution — a warning about adding new scout-imported python modules
> — is folded into the Gotchas below so nothing is lost in the merge.

## Scope

Make `fleet-state-scout`, `fleet-queue-ingest`, and `fleet-claim` always run
the **merged** version of the fleet scripts (and the shared parser they
import), so a fleet-script fix that lands on `origin/master` takes effect on
the next scout cycle **without a manual pull** — and, when the clone is
genuinely stuck, fails **loud** instead of silently false-freeing a
blocked task.

This is the active re-fix for fix-006 (`state-cache-lag`), the root cause
behind ~70/72 fleet-feedback entries in the last review window. It is **not**
another `blocked_by` parser tweak — the parser fix (#1783 / `ae6a5d28`) is
correct but inert.

## Verified current state (confirmed this iteration, linux host)

Investigated the actual code paths rather than trusting the issue body's
"reads the stale working tree" framing — the real mechanism is subtler:

1. **Issue/PR *data* is already read live via the `gh` API**, not from the
   local working tree. `fleet-state-scout` (`fetch_prs` / `fetch_issues_by_label`,
   ~L127–155), `fleet-queue-ingest` (`gh issue view`, ~L289–305), and
   `fleet-claim` (`check_blockers` probes blockers live via `gh issue view`,
   ~L540–563) all hit GitHub. So the issue body's "(b) read state from
   `origin/master` refs" is moot for the *data*.

2. **The stale thing is the script *code itself*.** All three import the
   shared parser as an on-disk Python module:
   - scout: `scripts/fleet/fleet-state-scout:37–40`
     `sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))` then
     `from fleet_blocked_by import parse_blocked_by, is_no_blocker_value`.
   - ingest: `scripts/fleet/fleet-queue-ingest:195–202`
     (`from fleet_blocked_by import blocker_refs, has_blocked_by_field`).
   - claim: `scripts/fleet/fleet-claim:504–573`
     (`sys.path.insert(0, os.environ["FLEET_LIB_DIR"])` →
     `from fleet_blocked_by import parse_blocked_by, blocker_refs`).
   The path resolves through the `~/bin/fleet-*` symlinks, which
   `install.sh` deliberately points at the **main clone**
   `~/src/IrredenEngine/scripts/fleet/` (install.sh:57–79 — worktrees get
   reset, so the main clone is the stable target). `fleet-claim:108–112`
   resolves `FLEET_LIB_DIR` by following that symlink.

3. **Nothing fast-forwards the main clone.** `fleet-up` Step 3d
   (fleet-up:1027–1063) `nohup`s the scout daemon with no preceding
   `git pull`/`fetch` on the main clone (it fetches *worktrees* at
   fleet-up:435–436/465–466, not the main clone). So the main clone's
   `master` only advances if a human/other op happens to pull it. On the
   mac host it sat 20 commits / >1 day behind (issue thread); on this
   linux host it was 1 behind / clean / on `master` at planning time —
   i.e. host-dependent and chronic.

4. **Second, independent gap — daemon import caching.** scout is a
   long-running daemon: `main()` loops `tick_once()` every 30s
   (`POLL_SECONDS_DEFAULT=30`, fleet-state-scout:75, main loop ~2169–2188).
   It imports `fleet_blocked_by` **once at module load**. So even after the
   on-disk file is fast-forwarded, the *running* scout keeps the old parser
   until the process is restarted. A naive "just `git pull` the clone" fix
   would silently miss this — the file would be fresh but the daemon
   wouldn't use it. (ingest and claim are short-lived — relaunched per
   scout tick / per worker iteration — so they re-import fresh code as soon
   as the on-disk file advances; only scout needs an explicit reload.)

## Approach (one approach, picked)

Guarded periodic fast-forward of the main clone in the scout loop +
scout self-`os.execv` when the scripts change + a no-network freshness
assertion in the short-lived consumers. This realizes the issue's
direction (a) (`git pull --ff-only` before scout cycles) made **automatic
and periodic** (so it can't "re-drift" the way a manual pull does — the
concern raised by worker-1/worker-2 in the thread), and adds the
fail-loud assertion the human asked for.

1. **New shared freshness helper** (single source of truth for the git
   guards; importable + tiny CLI). See the Gotchas note on module
   resolution before deciding the file form — `scripts/fleet/fleet_freshness.py`
   (python, beside the existing `fleet_blocked_by.py`) is the default; a
   sourced bash `fleet-clone-freshness.sh` (the #1821 form) is the
   zero-new-python-import fallback. Either way it exposes:
   - `refresh_main_clone(engine: Path) -> dict` — guarded ff:
     - SKIP (return `{"skipped": "off-master:<ref>"}`) unless
       `git -C engine symbolic-ref --short HEAD` == `master` (not detached,
       not a feature branch).
     - SKIP (`{"skipped": "dirty"}`) if `git -C engine status --porcelain`
       is non-empty.
     - `git -C engine fetch origin master --quiet`.
     - `old = HEAD`, `new = origin/master`. If equal → `{"advanced": False}`.
     - SKIP (`{"skipped": "diverged"}`) unless
       `git -C engine merge-base --is-ancestor HEAD origin/master` (true
       fast-forward only — never `reset --hard`, which would nuke a
       checked-out branch's work).
     - `git -C engine merge --ff-only origin/master`.
     - `scripts_changed = not (git -C engine diff --quiet old new -- scripts/fleet)`.
     - return `{"advanced": True, "old": old, "new": new, "scripts_changed": scripts_changed}`.
   - `assert_fresh(engine: Path) -> (bool, reason)` — **no network**:
     `git -C engine merge-base --is-ancestor origin/master HEAD`. False ⇒
     HEAD is behind the last-fetched `origin/master` ⇒ stale (the bug).
     Compares against the *locally-known* ref, so it can't deadlock on a
     network outage and won't false-nag during the normal fetch-lag window
     (a just-merged commit not yet fetched still leaves HEAD == local
     origin/master).
   - `__main__`: `--refresh` (run `refresh_main_clone`, print JSON, exit 0)
     and `--assert` (exit non-zero + stderr if stale) so bash callers reuse
     the same guards.

2. **scout (`fleet-state-scout`)** — `from fleet_freshness import refresh_main_clone`
   beside the other imports (~L40). In `main()`:
   - Before the first `tick_once()` and then every `K` ticks (K=4 ⇒ ~2 min,
     to bound `git fetch` cost vs. the 30s tick), call
     `refresh_main_clone(ENGINE)`.
   - On `scripts_changed` only, `log()` a loud
     `re-exec: scripts advanced <old>..<new>` line and
     `os.execv(sys.executable, [sys.executable, os.path.realpath(__file__)] + sys.argv[1:])`.
     Re-exec only when `scripts/fleet` actually changed — a plain `advanced`
     with no script delta (doc PRs, game merges, etc.) must NOT re-exec, or
     the daemon needlessly restarts (and drops ~30s of cadence) every time
     master moves. Re-exec keeps the same PID (so `scout.pid` stays valid)
     and happens **between ticks** (state.json is written atomically each
     tick via `write_atomic`, so no torn state). `os.path.realpath(__file__)`
     resolves the symlinked launch to the now-updated main-clone file.

3. **fleet-up (`Step 3d`, ~L1040)** — call the helper's `--refresh` CLI
   once **before** `nohup`ing scout, so the daemon starts on fresh code:
   `python3 "$ENGINE/scripts/fleet/fleet_freshness.py" --refresh || true`
   (best-effort; the periodic in-loop refresh covers steady state).
   Optionally also call it once per `fleet-dispatcher` main-loop pass (the
   #1821 placement) as defense-in-depth — harmless if scout already
   advanced the clone, and it covers the window if the scout daemon is
   wedged. Not required for the fix; scout's periodic refresh is the
   primary home.

4. **fleet-claim (`check_blockers` inline python, ~L504)** — call
   `assert_fresh`. If stale, **refuse the claim** (exit non-zero) with a
   loud diagnostic (`STALE FLEET SCRIPTS — refusing claim to avoid
   false-grant; scout will self-refresh, retry shortly`). Refusing (not
   false-granting) is the safe direction; staleness is transient
   (≤ refresh cadence) so this self-clears.

5. **fleet-queue-ingest (`STAMP_PY`, ~L195)** — call `assert_fresh`. If
   stale, **skip** the blocked/unblock label pass this run and log loud
   (don't stamp potentially-wrong `fleet:blocked` state). Normal case:
   scout has already refreshed before launching ingest, so it's fresh.

6. **Tests** — add `scripts/fleet/tests/` coverage for the freshness helper:
   off-master → skip, dirty → skip, behind+clean+on-master → advance,
   diverged → skip, up-to-date → no-op. Use a throwaway temp git repo with
   a fake `origin`.

## Affected files

- `scripts/fleet/fleet_freshness.py` — **NEW**: `refresh_main_clone`,
  `assert_fresh`, `--refresh`/`--assert` CLI. (If the bash-helper fallback
  is chosen instead, this is `scripts/fleet/fleet-clone-freshness.sh` +
  an install.sh symlink — see Gotchas.)
- `scripts/fleet/fleet-state-scout` — import + periodic guarded refresh +
  `os.execv` re-exec in `main()` (on `scripts_changed` only); one refresh
  before the first tick.
- `scripts/fleet/fleet-up` — one-shot `--refresh` before launching scout
  (Step 3d, ~L1040).
- `scripts/fleet/fleet-claim` — `assert_fresh` in `check_blockers`; refuse
  claim (loud) when stale.
- `scripts/fleet/fleet-queue-ingest` — `assert_fresh` in `STAMP_PY`; skip
  label pass + log loud when stale.
- `scripts/fleet/fleet-dispatcher` — (optional) per-pass `--refresh` as
  defense-in-depth.
- `scripts/fleet/tests/` — freshness guard tests.

## Acceptance criteria

- After a fleet-script fix merges to `origin/master`, within one refresh
  cadence (~2 min) the running scout re-execs onto the new code with no
  manual pull. Verify:
  `git -C ~/src/IrredenEngine merge-base --is-ancestor <fix-sha> HEAD` → true,
  and `scout.log` shows the `re-exec: scripts advanced …` line.
- A genuinely-blocked task whose body carries the **non-bold / inline**
  `Blocked by: #N` prose form (the coverage #1783 added) projects
  `blocked=true` / `blocked_by=#N` in `state.json`, and
  `fleet-claim … claim <N>` refuses.
- The guarded refresh **skips loudly** (no error, logged reason) when the
  main clone is off-`master`, detached, or dirty — never disrupts a
  checked-out branch (no `reset --hard`, ff-only).
- A plain master advance with **no** `scripts/fleet` change does **not**
  trigger a scout re-exec (the nit: re-exec gates on `scripts_changed`).
- Freshness helper tests pass (the 5 guard cases above).

## Gotchas

- **Daemon import caching is the easy thing to miss.** Fast-forwarding the
  file is necessary but not sufficient for scout — it must `os.execv` to
  reload. The whole bug recurred precisely because the merged parser file
  existed somewhere but the *running* consumer didn't use it. Note the
  re-exec reloads the **existing** `fleet_blocked_by` import (the real
  daemon-staleness fix) and is independent of where the new freshness logic
  lives — it works even if the freshness helper is the bash form.
- **New scout-imported python module — resolution fragility (#1821's
  warning, #1750/#1578).** The #1821 plan cautioned against adding a *new*
  scout-imported python module because scout resolves its lib dir via
  abspath-of-symlink and `FLEET_LIB_DIR` has mis-resolved before (#1578).
  Reconciliation: scout **already** imports `fleet_blocked_by` from that
  same dir successfully, so a sibling `fleet_freshness.py` placed beside it
  resolves through the *identical* mechanism — it shares the existing
  failure mode rather than adding a new one (a FLEET_LIB_DIR mis-resolution
  would break `fleet_blocked_by` too, independent of module count). So the
  python form is acceptable **iff** the new file lives in the same
  `scripts/fleet/` dir as `fleet_blocked_by.py`. If the implementer wants
  zero new python imports in the daemon, fall back to the #1821 bash form
  (`fleet-clone-freshness.sh`, sourced by-dir like `fleet-common.sh`, with
  an `install.sh` symlink) for the shared ff/assert logic — the os.execv
  re-exec stays either way.
- **Edit in your worktree, not the main clone.** `fleet-guard-worktree-edit`
  denies writes escaping the worktree root, and editing the main clone's
  `scripts/fleet/` directly would be live-editing the running fleet. Work
  in `…/.claude/worktrees/worker-N/scripts/fleet/` (relative paths — cf.
  the worktree absolute-path trap).
- **Bootstrap chicken-and-egg.** The fix lives in scripts that themselves
  run from the stale clone, so deploying it needs ONE manual `git -C
  ~/src/IrredenEngine merge --ff-only origin/master` (or a fleet-up restart)
  the first time. Self-healing thereafter — call this out in the PR body.
- **Don't fetch every 30s tick** — gate to every K ticks. A quiet
  single-ref fetch is cheap but per-tick is wasteful.
- **ff-only, never reset.** The main clone is a *shared* checkout (issue
  body: "agents switch branches in" it). Only advance when on a clean
  `master` and HEAD is a strict ancestor of `origin/master`.
- **`assert_fresh` is no-network on purpose** — it compares against the
  last-fetched `origin/master`, so it can't deadlock the fleet during a
  GitHub outage and won't nag during normal fetch lag.
- **Residual / future hardening (out of scope here):** if the main clone
  turns out to be left off-`master` for long stretches (rare — workers use
  worktrees), the guarded ff will keep skipping. The fully robust fix would
  materialize `scripts/fleet/` from `origin/master` into a dedicated,
  never-checked-out cache dir and point `FLEET_LIB_DIR` there (install.sh
  symlink retarget). Not needed now; the freshness assertion makes the
  off-master case fail loud rather than silent. Note it if the skip-loud
  path starts firing in feedback.
- **One task, not a stack.** All edits live in `scripts/fleet/` and are
  tightly coupled (new helper + 4–5 consumers); a partial landing doesn't
  fix the bug, so splitting only adds rebase latency.

## Sibling / in-flight reconciliation

- **#1821 (plan, MERGED first)** — same fix, less detail; this plan
  supersedes it (the merged-plan file now carries this content). Its one
  distinct contribution (the new-python-module fragility warning) is folded
  into the Gotchas above. If a separate #1822 plan PR is still open for
  #1810, it is now redundant with this — flag for the human/merger.
- **#1749 / #1783 (blocked_by parser, MERGED)** — this is the **deployment**
  complement, not a duplicate; the parser is correct but inert until the
  clone advances.
- **#1750 (state.json atomicity) and #1751 (stackable base guard)** —
  distinct families per the issue body; no surface overlap.
