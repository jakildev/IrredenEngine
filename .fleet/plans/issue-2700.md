# Plan: per-kind trigger suppression for the union worker lane (rev 2)

- **Issue:** #2700
- **Model:** opus
- **Date:** 2026-08-08

Rev 2, superseding the 2026-07-31 plan per the plan-review bounce. Everything
the review marked keep-verbatim carries forward. Three deltas: the Phase 2 fire
rule is now the membership test, picked deliberately; verification of the
review's item 3 shows that rule **requires a dispatcher-side amendment** (new
Phase 3); and a fact both prior passes missed forces an explicit role allowlist.

## Scope

Make the transition-to-nothing-to-do suppression in `update_role_trigger`
**per sub-lane with a new-item membership test**, scoped to the `worker` role
by explicit allowlist — plus **one amendment to the dispatcher's steady-state
trigger consumption**, which the verification below shows is load-bearing for
the membership rule, not optional hardening. Rev 1 scoped this to "the wake
predicate only"; that scope is not honestly achievable with the rule that
covers the dominant wake class, because today's queue drainage is *pumped by*
the claim-churn wakes this issue removes. The dispatcher change is the
replacement pump.

Still out of scope: `project_worker`'s item-level filtering (#2114 / #2287 /
#1726 — all correct as-is), the empty-exit backoff itself (#2698, unchanged and
still wanted as the spin bound), and the `fleet_task_class` claimable gate
(#2696, unchanged).

**One task, one PR.** Splitting the scout rule from the dispatcher retention
would ship a throughput regression in the gap between them (see Phase 3
rationale) — they land together.

## Verified current state (source-read at `a9459315b`)

- `update_role_trigger` — `fleet-state-scout:2690`; whole-projection
  suppression `if not projection:` at 2724; `touch()` at 2730; seen file is a
  bare 16-hex `stable_hash` (:1199, sha256 of sorted JSON, truncated).
- `project_worker` — :1922, registered in `PROJECTORS` (:2391). Emits four
  `kind`s: `task` `{kind, repo, id, blocked_by}` (:1935), `needs_plan` (:1957),
  `pr` `{…, labels: sorted(labels & _WORKER_RELEVANT_LABELS)}` (:1983),
  `semantic_conflict` (:1989). All items are dicts carrying `kind`.
- Trigger loop at :3037; `queue-manager` / `queue-manager-ingest` branch to
  inline bare-hash compares before the `elif update_role_trigger(...)` at :3132
  — untouched by this change.
- Slicers write `projections/<role>.json` every tick **regardless of
  suppression** (:3136-3145), so the dispatcher's resolver always reads a fresh
  claimable set. This is what makes Phase 3 sound.
- **Correction to the issue's AC-3 premise:** opus-reviewer items are NOT all
  kind-less — `project_opus_reviewer` emits kind-less PR items *and*
  `{kind: "plan_review"}` issue items (:2035). `epic_steward` items all carry
  kinds (:2369-2386). Only sonnet-reviewer, merger, and smoke-worker are
  kind-free. So "bucket by item shape, single-lane roles reduce to one bucket"
  is false for two roles: shape-keyed bucketing would silently change
  opus-reviewer and epic-steward suppression semantics. Hence the allowlist.
- **Dispatcher trigger lifecycle** (`fleet-dispatcher`):
  - Defer paths that KEEP the trigger: role at concurrency cap (:1613), no idle
    pane, resolver defer (:1720), class saturated (:1763), min-gap
    (:1958-1963), other classes queued (:1988), zero dispatched (:1999).
  - CONSUME paths: empty-exit backoff (:1946, #2698) and **steady-state success
    (:1994) — `dispatched > 0`, past boot window, no other class queued — which
    consumes even when the claimable count exceeds this tick's launches** (the
    "headroom is filled" comment above it is wrong in exactly this case; the
    fan-out loop also exits on idle-pane exhaustion).
  - Periodic worker safety rearm (:2151-2196, `PERIODIC_REARM_INTERVAL_SECONDS`
    = 300, :397): re-arms only when no trigger is pending AND **zero workers are
    active** (:2179) AND the resolver would dispatch a concrete class (:1518).

## The fire rule: membership test, and why

**Fire iff some kind's new item set contains an item absent from that kind's old
set.** Item identity = `stable_hash(item)` over the whole projector dict.
Per-kind state stored as the fmt-2 seen payload (Phase 2).

- Pure removal — a task claimed, a feedback label cleared, a `fleet:resolving-*`
  claim dropping a conflict item, a merge closing an issue — is suppressed
  **even when the lane stays non-empty**. This is the dominant wake class and
  the issue's central case; rev 1's non-empty rule missed it entirely.
- A modification (a task's `blocked_by` going `#2385` → `(none)`, a PR's verdict
  label swapping) hashes as remove+add ⇒ the new item is absent from the old set
  ⇒ fires. Unblocking and re-verdicts are genuinely new work.
- The rejected alternative — keep rev 1's cheaper non-empty rule and narrow the
  scope to drain-to-zero wakes — is honest but keeps the highest-frequency wake
  source (task claims against a ~30-item lane) at ~100% of today's rate. With
  each wake fanning out to every idle pane, the membership payload (~1 KB of
  hashes for a ~50-item projection) is cheap against that; rejected.

A false-positive direction note: any projector field change fires (the item
re-hashes). That is the safe direction — over-firing costs one no-pick
iteration, over-suppressing idles the fleet.

## Why the membership rule requires the dispatcher amendment

The review's mitigating argument was "the dispatcher keeps the trigger when it
defers at cap, so a freed pane re-evaluates without a fresh wake." The
defer-at-cap retention is real (:1613) — but it only covers ticks that
*deferred*. The common drain path is the steady-state consume:

1. N claimable tasks arrive ⇒ scout fires. Dispatcher fans out to all idle panes
   (say 4 of N=30), `dispatched=4 > 0`, steady state ⇒ **trigger consumed**.
2. Each pane's claim removes a `task` item. Today that flips the hash and
   re-arms the trigger, so freed panes keep getting dispatched — **the
   claim-churn wake is the drainage pump.** Under the membership rule those
   re-arms are exactly what gets suppressed.
3. With no pending trigger, a freed pane is only rescued by the periodic rearm —
   which requires **zero active workers**. One long-running task blocks
   re-dispatch of every freed pane for its whole duration; drainage degrades
   from continuous to cap-sized waves at ≥300 s granularity.

So Phase 3: at the steady-state consume, for claim-counted lanes
(`claim_headroom >= 0`), **consume only when this tick's launches covered the
claimable headroom (`dispatched >= claim_headroom`); otherwise keep the
trigger** and log it. The uncapped path (`claim_headroom == -1`) keeps today's
consume-on-success, so only the worker lane changes. A retained trigger is cheap
and already-bounded: with no idle pane, `dispatch_role` returns above the
resolver; when a pane frees, it is dispatched from the retained trigger and the
resolver re-reads the fresh projection slice; if the remaining claimable items
are ones every worker refuses, the empty-exit backoff (#2698) consumes the
trigger after the streak cap exactly as today.

## Role scoping

New module-level `PER_KIND_TRIGGER_ROLES = frozenset({"worker"})` beside
`PROJECTORS`. `update_role_trigger` takes the per-kind path only for roles in
the allowlist; every other role — including the kind-carrying opus-reviewer and
epic-steward — keeps the existing bare-hash whole-projection code path
byte-for-byte. Extending suppression to opus-reviewer is real but unmeasured
work — file it as a follow-up once the worker lane proves out. A pleasant
consequence: the existing `test_role_trigger_empty.py` suite runs under role
name `"r"` (not allowlisted), so it becomes the byte-identical regression proof
for the legacy path with zero modification.

## Approach

**Phase 0 — baseline probe.** Confirm `worker` shows `suppressed == 0`
pre-change. Bail path: any worker suppression ⇒ premise wrong ⇒ stop, post the
reading, re-plan. Keep the counter as a permanent `log()` diagnostic —
post-change it doubles as the live evidence.

**Phase 1 — scout: per-kind partition behind the allowlist.** For allowlisted
roles, bucket projection items by `item.get("kind", "")` (defensive default
bucket). Compute per-item `stable_hash`, per-kind sorted hash lists.

**Phase 2 — scout: membership fire rule + fmt-2 seen file.** Payload:
`{"fmt": 2, "kinds": {"<kind>": ["<item-hash>", ...]}}`, written via the
existing `write_atomic`. Rule: unchanged payload ⇒ early-return False, no write.
Changed ⇒ write, then fire iff any kind has `set(new) - set(old)` non-empty;
else write the `.empty-suppressed` marker and return False. The empty-projection
case falls out. Marker semantics widen from "last write was an empty-projection
suppression" to "last write was a suppression (no new items)". Legacy read: a
bare-hex or unparseable seen file ⇒ all per-kind sets absent ⇒ every non-empty
kind fires ⇒ at most one extra fire on upgrade (the deliberately safe direction;
the opposite error is #561's permanently-idle mode).

**Phase 3 — dispatcher: retain-while-uncovered.** Gate the steady-state
`rm -f "$trigger_file"` on coverage; on the keep branch, log once per window
(reuse the `CAP_DEFER_LOGGED` pattern). Every other consume/defer path is
untouched, including the empty-exit backoff.

**Phase 4 — `fleet-debug` + tests.** Parse both seen formats; for fmt-2 roles
show per-kind item counts.

## Affected files

- `scripts/fleet/fleet-state-scout` — per-kind path, `PER_KIND_TRIGGER_ROLES`,
  module-level helpers (the test suite loads the script via `SourceFileLoader`)
- `scripts/fleet/fleet-dispatcher` — steady-state consume gate (Phase 3)
- `scripts/fleet/fleet-debug` — dual-format seen parsing + per-kind display
- `scripts/fleet/tests/test_role_trigger_empty.py` — new per-kind cases
- `scripts/fleet/tests/test_fleet_debug_triggers.py` — fmt-2 fixture
- `scripts/fleet/tests/test_dispatcher_claimable_cap.sh` — retention cases
- `.fleet/plans/issue-2700.md` — this plan

## Acceptance criteria

1. **Pure removal from a still-non-empty lane suppresses**: `task×2 → task×1`,
   other kinds unchanged ⇒ new hashes recorded, marker written, trigger NOT
   touched.
2. Sub-lane drain to zero with another lane non-empty (`pr×1 → pr×0`, `task×N`
   unchanged) ⇒ suppressed.
3. Positive controls (the fix cannot pass by never firing): `pr×0 → pr×1` fires;
   an item modification (same `id`, `blocked_by` changed) fires; a simultaneous
   remove+add across two kinds fires.
4. Whole-projection-empty transition still suppresses; empty → non-empty still
   fires (existing tests, unmodified).
5. Non-allowlisted roles byte-identical: the existing suite under role `"r"`
   passes unmodified, plus one new case feeding an opus-reviewer-shaped
   projection (mixed kind-less + `plan_review` items) through a non-allowlisted
   role and asserting legacy behavior.
6. Upgrade: seen file containing a legacy bare-hex hash ⇒ exactly one fire on
   the next non-empty tick, fmt-2 written, quiet thereafter; never a
   permanently-idle role.
7. Dispatcher: a steady-state tick with `dispatched < claim_headroom` keeps the
   trigger; `dispatched >= claim_headroom` consumes; uncapped lanes
   (`claim_headroom == -1`) consume as today;
   `test_dispatcher_empty_exit_backoff.sh` passes unmodified.
8. Phase 0 probe reading posted on this issue (pre-change: worker
   `suppressed == 0`).
9. `fleet-positive-control` run for the new scout cases against the pre-fix ref
   reports MEANINGFUL (the pure-removal case fires pre-change).
10. **Live post-change check** (restored from rev 1 per plan-review binding
    constraint 1): against the real worker projection's item shapes, a pure
    removal of one `pr` item while `tasks_open` stays non-empty suppresses and
    writes the `worker.empty-suppressed` marker. Corroboration only — the
    hermetic cases (1-3) are the real proof — but it is the one criterion that
    would catch a rule correct in a fixture and inert against live item shapes.
    Assert on `touch()` behaviour in the tests; treat the marker as the
    live-run signal.

## Gotchas

- **Over-suppression is the dangerous direction** — it silently idles the fleet.
  Phase 3 is the counterweight, not polish: do not land Phase 2 without it. The
  residual safety nets (300 s rearm at zero-active, empty-streak backoff,
  defer-path retention) all remain as backstops.
- The seen-file compare must be on the **serialized fmt-2 payload**, not the
  legacy whole-projection hash — otherwise the early-return path re-fires
  forever on a stable projection after upgrade.
- Never clear the `.empty-suppressed` marker on the unchanged-hash early return
  (no write happened there — the marker means "the most recent *write* was a
  suppression").
- `queue-manager` / `queue-manager-ingest` inline compares are deliberate
  non-consumers of the suppression — don't "unify" them.
- The dispatcher edit sits between `LAST_ROLE_DISPATCH_COUNT` bookkeeping and
  the `else` branch — keep the boot-window and `DISPATCH_MORE` keeps above it
  untouched, and keep the new condition off the uncapped path.
- **`fleet_poll_topology` followers** (binding constraint 2): `:28` and `:77`
  recompute `seen-hashes/` on followers rather than shipping them, so there is
  no cross-host fmt-2 migration. A follower's first post-upgrade tick is the
  ordinary legacy-read path (one fire, settles).
- **Re-anchor by symbol, not line** (binding constraint 3): with #2915 / #2934 /
  #2953 in flight, line numbers drift. Grep the targets at implementation time.
- Engine-public artifact: keep issue references engine-side only.

## Task shape

Single opus task, one PR. The scout rule, dispatcher retention, debug surface,
and tests form one contract change; any split ships a regression window.
