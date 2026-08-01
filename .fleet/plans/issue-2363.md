# fleet-clone-freshness — persistent-skip escalation + recoverable-park self-heal (re-plan after #2381)

**Issue:** #2363
**Repo:** jakildev/IrredenEngine
**Model:** opus — every-tick mutating guard on the shared main clones of all
hosts, plus bash-3.2 portability; fully specified below, but the blast radius of
a wrong unattended mutation keeps it above sonnet. No novel design → not fable.
No C++ build is required (pure bash + the existing test harness), so any host —
including macOS — can implement and verify this.
**Date:** 2026-07-20 (plan), implemented 2026-07-30

**Supersedes the 2026-07-13 `## Plan` comment on the issue.** That plan predates
PR #2381 (merged 2026-07-14), which shipped its Fix part 1 (reviewer-scratch
self-heal + loud FROZEN warn). The remaining scope is (1) persistent-skip
escalation and (2) the self-heal scope decision for the 2026-07-15 recurrence
(clean feature-branch park). This plan covers exactly those two.

## Verified current state (origin/master, 2026-07-20)

- `scripts/fleet/fleet-clone-freshness.sh` `advance_main_clone`: 60s fetch
  sentinel (`${FLEET_STATE_DIR:-~/.fleet/state}/.${repo_tag}-clone-advanced`,
  `repo_tag="$(basename "$root")"`) → Guard 1 self-heals only
  `claude/*-reviewer-scratch` with fully-clean porcelain (checkout master,
  `branch -D` the junk ref, fall through); **any other** non-master branch → loud
  `… is on '<branch>' (not master) — skipping advance. master is FROZEN …` +
  `return 0`, re-emitted every warn-eligible tick forever. Guard 2's dirty warn
  likewise repeats. The diverged warn lives in the shared tail
  `_ff_advance_to_origin_master`, which `restore_main_clone_to_master` (the
  fleet-up path) also calls.
- No rate-limit, no escalation, no alert file anywhere in the guard — the only
  durable signal is stderr spam.
- Callers (interface unchanged by this plan): `fleet-dispatcher` (per tick,
  ENGINE then GAME), `fleet-up` (`restore_main_clone_to_master`, both repos),
  `fleet-claim` (`assert_clone_fresh` on `FLEET_ENGINE_ROOT` — a
  stale/parked/diverged *engine* clone refuses **every claim on both repos**;
  confirmed live in the 2026-07-15 thread datapoint, ~30 min fleet-wide freeze).
- Alerts-dir conventions (shipped): `fleet-up` creates `~/.fleet/alerts`;
  `witness` owns `<agent>.stuck` / `fleet-dispatcher.stuck` and self-clears them;
  `fleet-rebase` (#2362, merged) writes a flat
  `${FLEET_ALERTS_DIR:-$HOME/.fleet/alerts}/fleet-rebase-hung-lock` file — single
  `printf` of `key=value` fields, best-effort `|| true`. A
  `clone-freshness-<repo_tag>` filename cannot collide with either.
- Test harness: `scripts/fleet/tests/test_clone_freshness.sh` T1–T14, local
  `file://` origin fixture, `FLEET_STATE_DIR` isolation, `reset_rate_limit`
  helper for the 60s sentinel — extend it, don't invent a new harness.
- Cadence measurement (grounds the N choice): warn-eligible ticks are gated by
  the 60s fetch sentinel → ~1/min steady-state from the dispatcher. Observed
  incidents: game clone parked ~2.5 days (issue body); engine clone parked
  **30 min** with claims refused fleet-wide until an unrelated fleet-up cleared
  it (2026-07-15 comments).

## Scope

Two changes in one PR, both inside `scripts/fleet/fleet-clone-freshness.sh` + its
test file; no caller edits, sourcing interface unchanged.

**A. Escalate-then-quiet for persistent identical skips** in `advance_main_clone`
— the load-bearing half: nothing in the loop noticed the 07-15 freeze.

**B. Self-heal scope decision (committed):** keep the tick-time self-heal inside
the fleet's **scratch namespace** — `claude/*-scratch`, which covers the pool
worktrees' `claude/pool-<N>-scratch`, its `claude/game-pool-<N>-scratch` twin,
and the legacy `claude/<role>-reviewer-scratch` — gated on a clean tree AND a
HEAD already contained by `origin/master` (no unique commits). That is the
issue's literal AC. The heal drops the junk ref (`-D`), as #2381 shipped.
Every other park — non-`claude/*`, a `claude/*` feature branch, a dirty tree, or
a scratch ref carrying a unique commit — keeps the warn+skip behavior, now
escalated by A instead of spamming.

Scope-line rationale (**corrected 2026-07-30**, PR #2668 opus recheck): an
earlier draft of this plan widened the heal to any `claude/*` branch with a
"provably recoverable" HEAD, on the premise that *non*-`claude/*` is the human
case. That premise is false. `docs/agents/FLEET.md` rule 1 states Cursor /
ad-hoc work uses `claude/<area>-<topic>`, so a human's deliberate session is
**inside** `claude/*`; and `commit-and-push` leaves exactly that session clean
and pushed while the human sits on the PR, which is precisely the state the
recoverability check green-lights. Healing it sends the human's next commit to
`master` (violating rule 1) or splits one slice across two PRs, with no notice
they ever see. Recoverability proves *git loses nothing*; it does not prove
*no session is using this checkout*. Only the scratch namespace — a
per-iteration throwaway that belongs solely in `.claude/worktrees/*` — proves
the latter, so the scope stays there. Genuine feature-branch parks (the 07-15
`claude/2428-frac-edge-coverage` case) are handled by A's alert, which is what
the issue prescribes: *"Keep skipping … when the tree is dirty or the branch is
a real WIP feature branch."*

Folding the tiers also retires dead code: the shipped tier-1 pattern
`claude/*-reviewer-scratch` matches nothing the live fleet produces (reviewer
and worker scratch refs are `claude/pool-<N>-scratch`; `grep -rn reviewer-scratch`
outside this script and its tests returns nothing), so every real scratch park
was already falling through to the over-wide tier 2. One `claude/*-scratch`
glob covers both spellings.

## Approach

All in `scripts/fleet/fleet-clone-freshness.sh`.

1. **Helpers (new, file-local).**
   - `_freshness_warn <root> <key> <message>` — escalate-then-quiet emitter for
     `advance_main_clone`'s skip warns. Counter file
     `${FLEET_STATE_DIR:-$HOME/.fleet/state}/.${repo_tag}-freshness-skip` holds
     one line `<count> <first_seen_epoch> <key>` (bash-3.2-safe
     `read -r count first key`; key = `<reason>|<branch>` with reasons `parked` /
     `checkout-failed` / `dirty` / `diverged`). Same key → increment; different
     key → reset to `1 <now> <newkey>`. count < N → emit `<message>` to stderr
     exactly as today. count == N → emit one loud `ESCALATION` line (naming the
     alert path, the remedy, and "suppressing further identical warns until the
     condition clears") and write the alert file. count > N → emit nothing. N
     defaults to **15** (~15 min of continuous skip at the measured ~1/min
     cadence — chosen against the 30-min 07-15 outage, which the issue body's
     "say 60" ≈ 1 h would have missed entirely); env override
     `FLEET_FRESHNESS_SKIP_ESCALATE_N` for tests/operators. All state writes
     best-effort (`|| true`); always returns 0.
   - `_freshness_all_clear <root>` — `rm -f` counter + alert file, best-effort.
   - Alert file:
     `${FLEET_ALERTS_DIR:-$HOME/.fleet/alerts}/clone-freshness-<repo_tag>` (flat
     filename per the shipped `fleet-rebase-hung-lock` convention; honor
     `FLEET_ALERTS_DIR` like `fleet-rebase` does, for test isolation). Content is
     a single printf line:
     `clone-freshness skip: host=… root=… branch=… reason=… count=… since=<ISO8601 from first_seen> remedy='…'`.
     `mkdir -p` the dir best-effort first.

2. **Wire the guards through the emitter** — in `advance_main_clone` only
   (`restore_main_clone_to_master` is a fleet-up one-shot and stays untouched;
   after any restore, the alert clears one tick later via the healthy path):
   - Guard 1 non-healed park → `_freshness_warn "$root" "parked|$branch"
     "<existing FROZEN line verbatim>"` (keep the literal "not master" text — T5
     greps it and operators grep logs for it).
   - Guard 1 checkout-failure → `_freshness_warn … "checkout-failed|$branch" …`.
   - Guard 2 dirty → `_freshness_warn … "dirty|master" …`.
   - Shared tail: add an optional second arg
     `_ff_advance_to_origin_master <root> [count_skips]`, passed only by
     `advance_main_clone`. When set: diverged → `_freshness_warn …
     "diverged|master" …`; advanced / already-current → `_freshness_all_clear
     "$root"`. When unset (restore path): byte-identical behavior to today. The
     transient "ff-only refused (concurrent git op)" warn stays uncounted and
     unclearing — the next tick resolves it either way.
   - Self-heal success paths fall through to the tail, so the clear happens
     there; no separate call needed.

3. **Scratch-namespace self-heal in Guard 1** (one tier, replacing #2381's
   `claude/*-reviewer-scratch` arm):
   `if [[ "$branch" == claude/*-scratch && -z "$dirty" ]] && git -C "$root"
   merge-base --is-ancestor HEAD origin/master`
   → `git checkout master --quiet`, `branch -D` the junk ref (per #2381's
   reviewed judgment that scratch refs hold nothing), one heal line, set
   `branch=master`, fall through to Guard 2 + tail.
   Any other branch, a dirty tree, or a scratch HEAD holding a unique commit →
   the step-2 `parked` warn (unchanged skip). Checkout failure → the existing
   `checkout-failed` warn + `return 0` (unchanged semantics). No
   `_head_recoverable` helper: its `origin/<branch>`-contains-HEAD arm is exactly
   the clean-and-pushed live-session state, and a scratch ref is never pushed, so
   the ancestor check alone is both necessary and sufficient here.

4. **Tests** — extend `scripts/fleet/tests/test_clone_freshness.sh` (reuse the
   fixture and `reset_rate_limit`; add `export FLEET_ALERTS_DIR="$TMPROOT/alerts"`
   alongside the existing `FLEET_STATE_DIR` isolation):
   - **T15 live-session regression:** clean `claude/render-glow-pulse` park whose
     HEAD is pushed to `origin/<branch>` (recoverable, but NOT an ancestor of
     origin/master) → **untouched**: branch + HEAD unchanged, no self-heal line,
     "not master" warn. This is the fixture the widened scope healed.
   - **T16 heal pool scratch:** clean `claude/pool-7-scratch` at an ancestor of
     origin/master → healed to master, ff-advanced, **junk ref deleted**,
     exactly one self-heal line.
   - **T17 scratch with a unique commit:** clean `claude/pool-8-scratch` holding
     one commit origin/master doesn't contain → untouched: "not master" warn,
     branch + HEAD unchanged.
   - **T18 dirty scratch park** → untouched (T5c pattern).
   - **T5b/T5c regression:** the legacy `claude/<role>-reviewer-scratch`
     spelling still heals / still refuses when dirty under the folded glob.
   - **T5 regression:** non-claude `feature/x` clean park → still skipped, still
     warns (already asserted — must stay green).
   - **T19 escalation:** `FLEET_FRESHNESS_SKIP_ESCALATE_N=3`; three warn-eligible
     parked ticks (`reset_rate_limit` between) → first two warn normally, third
     emits the ESCALATION line AND the alert file exists containing
     `reason=parked` and `count=3`; a fourth tick emits **no** skip line.
     Positive-fire: asserts the alert file's presence + fields, not just absence
     of spam.
   - **T20 key-change reset:** switch the park to a different branch → counter
     resets, warn re-emitted normally.
   - **T21 clear:** fix the clone (checkout master) → next tick advances and both
     counter + alert file are gone.

## Affected files

- `scripts/fleet/fleet-clone-freshness.sh` — helpers + guard wiring + the
  scratch-namespace heal
- `scripts/fleet/tests/test_clone_freshness.sh` — T15–T21
- Nothing else: no caller edits (`fleet-up` / `fleet-dispatcher` / `fleet-claim`
  source the same functions), `install.sh` already symlinks the file,
  `~/.fleet/alerts` is already created by `fleet-up`.

## Acceptance criteria

1. Clean `claude/*-scratch` park whose HEAD is contained by origin/master: next
   warn-eligible tick restores master, ff-advances, deletes the junk ref, logs
   exactly one self-heal line (T16, T5b fire).
2. Anything else — a `claude/*` feature branch (even clean and pushed), a dirty
   tree, a scratch ref with a unique commit, a non-`claude/*` branch: no
   mutation, warn+skip preserved (T15/T17/T18/T5/T5c).
3. The same skip persisting N warn-eligible ticks: exactly one ESCALATION line +
   an alert file with host/root/branch/reason/count/since/remedy; identical warns
   suppressed afterwards; the alert **refreshed every subsequent tick** (honest
   `count=`, and a human-cleared alert re-appears while the condition holds);
   counter + alert removed when the condition clears (T19/T21/T22 fire —
   positive-fire on file presence and fields).
4. `bash scripts/fleet/tests/test_clone_freshness.sh` fully green (T1–T22) on
   macOS bash 3.2 and Linux.
5. `restore_main_clone_to_master` behavior byte-identical (fleet-up path
   untouched); every entry point still always returns 0 under `set -e` callers.

## Gotchas

- "Consecutive ticks" = **warn-eligible** ticks (the 60s fetch sentinel
  early-returns otherwise) — tests MUST `reset_rate_limit` between calls or the
  counter never moves.
- bash 3.2 (macOS): no associative arrays; `[[ $branch == claude/* ]]` glob
  matching is fine; `read -r count first key` handles the 3-field counter line.
- The guard fetches **`origin master` only**, so `origin/<branch>` may be locally
  stale for the recoverability check — safe *because the branch ref is never
  deleted*: a wrong staleness guess costs a re-checkout, never commits. Do not
  "fix" this by fetching all refs; the guard must stay cheap on every tick.
- Keep the literal "not master" / FROZEN warn text on the non-healed park path —
  T5 greps it.
- Do not wire counting into `restore_main_clone_to_master` — it's a fleet-up
  one-shot; counting there would double-count or spuriously clear. The alert
  clears on the next dispatcher tick's healthy pass after any restore.
- Alert and counter writes are best-effort (`|| true`) — a read-only `$HOME` must
  never break the advance path.
- Bootstrap irony (#1810): the merged fix is inert until each host's main clone
  advances past the merge once — after merge, run `fleet-up` (or
  `git -C <main-clone> merge --ff-only origin/master`) on any host currently
  wedged in the skip loop. Note this in the PR body.

## Plan-review

Vetted 2026-07-20 (opus plan-review pass): `fleet-plan-lint` PASS, load-bearing
citations spot-checked against `origin/master`, approach assessed design-sound.
`human:review-plan` was flagged for two approach calls — the scope of unattended
mutation on the shared main clones, and the deliberate N=15 vs the issue body's
"say 60" — and has since been cleared, along with `fleet:plan-review`.
