## Plan

- **Issue:** #2699
- **Model:** opus — dispatcher scheduling semantics; the fix is small but the
  policy choice (fairness floor vs. churn-signal coupling) needs judgment
- **Date:** 2026-07-31

### Verified current state

Read from `scripts/fleet/fleet-dispatcher` at `origin/master` `d6a44197`, plus
`~/.fleet/logs/dispatcher.log`, `~/.fleet/state/dispatch/*.json`, and live runs
of `fleet_task_class.py`. All numbers in the issue body were measured, not
inferred:

- The saturation test is `claim_headroom = DISPATCH_COUNT - class_racing` at
  **1580-1583**; the fan-out that serves another class is at **1586-1591** and
  is reached **only** when `claim_headroom == 0`.
- `class_racing` = `count_recent_active_for_class` (**860-878**), which counts a
  dispatch record only while `age < CLAIM_SETTLE_SECONDS` (**481**, default 90).
- The role concurrency cap (**1503-1508**) counts `count_active_for_role` with
  **no age filter**. The two counters therefore measure different populations,
  and worker iterations outlive the 90s window (measured record ages at
  01:40Z: 92s / 214s / 303s / 482s ⇒ `class_racing = 0` with all four panes
  occupied).
- The settle window is deliberate and its rationale is stated at **470-481**.
  Its explicit premise — *"the task leaves tasks_open once claimed, which is
  the real signal"* — is what fails: a **worker-refused** item is never claimed,
  so it never leaves the slice and its count contribution is permanent.
- Consequence measured, not assumed: `serving next class` last fired
  **2026-07-29T10:25:35Z**; `dispatched class=` over the last 2h is 57 opus /
  0 sonnet / 0 fable.

**Explicitly NOT verified (and a phase below measures it):** I confirmed the
resolver *reports* 10 claimable sonnet items and 21 fable planning candidates.
I did **not** verify those items are genuinely claimable by a fresh worker —
they could be fictional in the same way the opus five are. Phase 1 measures
this before any behaviour change, because it decides whether the fix yields
real throughput or just relocates the churn.

### Approach

Two candidate fixes; this plan ships **A** and records **B** as the convergence
target, deliberately *not* folding B in.

**A — fairness floor in the election (shipped here).** Track consecutive ticks
in which `dispatch_role` elected the same class while `DISPATCH_MORE=1` (i.e.
another class *is* claimable). Once that run reaches a threshold, add the class
to `_excluded` for one pass so the existing 1586-1591 fan-out serves the next
class. Self-contained: no new state source, no dependency on an unlanded fix,
and it reuses the loop's own bounded-exclusion mechanism.

**B — couple to the empty-exit streak (#2698, not in scope here).** A
per-`(role, class, host)` streak is the precise signal — it demotes a class
because workers actually claimed nothing, rather than because it has had many
turns. B subsumes A's intent but depends on #2698, whose plan is currently
bounced to `fleet:needs-plan`. Shipping A first gives a floor that does not
regress if B lands later; when B lands, A's threshold can be raised or A
retired. **A is a floor, not a substitute for B** — this plan does not claim
otherwise.

Rejected: widening `CLAIM_SETTLE_SECONDS` or switching `class_racing` to
occupancy. Both regress the stated 470-481 intent ("a worker busy on task A
must not block dispatching a worker for task B"), and neither fixes the case
where `DISPATCH_COUNT > cap`, in which headroom stays positive even at full
occupancy.

### Phases

1. **Measure the downstream lanes first (no behaviour change).** For the 10
   sonnet items and the fable plan-pick list, evaluate each against the same
   predicates `role-worker.md` step 3 applies (claim label, `Blocked by:`,
   open-PR cross-check, host gate). Record how many a fresh worker would
   actually claim. If that number is 0, **stop and re-scope** — the defect is
   then projection-side, not election-side, and A would only move the churn.
   Post the count on the issue either way.
2. Add the consecutive-election counter next to the existing
   `CAP_DEFER_LOGGED` bookkeeping: reset when the elected class changes or when
   `DISPATCH_MORE=0`; increment on each tick that elects the same class with
   `DISPATCH_MORE=1`. Keep it in-memory (daemon-lifetime), consistent with
   `CAP_DEFER_LOGGED`; no new state file.
3. Wire it into the loop: when the counter is at/over threshold, seed
   `_excluded` with the elected class before the first `resolve_worker_class`
   pass, and log the yield with the class and run length. Threshold behind
   `FLEET_DISPATCHER_CLASS_FAIRNESS_RUN` (default 3), validated-and-clamped the
   same way `BOOT_FANOUT_WINDOW_SECONDS` / `CLAIM_SETTLE_SECONDS` are
   (**442-450**, **482-486**) — a non-numeric override must not kill the daemon
   under `set -u`.
4. Tests in `scripts/fleet/tests/`: positive-fire (AC 1) and negative control
   (AC 2) per the issue. Extend the existing dispatcher suite rather than
   adding a new harness.

### Affected files

- `scripts/fleet/fleet-dispatcher` — config block near 442-486; counter
  bookkeeping near 850-880; the loop at 1554-1599.
- `scripts/fleet/tests/` — dispatcher suite (exact file confirmed at
  implementation time; do not assume a filename from this plan).
- `~/.fleet/fleet-up.conf` documentation block — document the new knob
  alongside the other `FLEET_DISPATCHER_*` overrides.

### Acceptance criteria

Inherits AC 1-3 from the issue body. AC 1 is the positive-fire criterion: the
test must observe a **non-elected class actually dispatched**, not merely that
the counter incremented.

### Gotchas

- **Run the dispatcher test suite before editing.** It shells through `~/bin`
  into the main clone and is not hermetic — a pre-existing red must be
  established first or it will be misattributed to this change.
- `dispatch_role` returns early on several paths (no trigger, role cap, defer).
  The counter must not be incremented on paths that never reached the election,
  or a quiet fleet will accumulate a fake run and yield spuriously.
- The loop already exits after 3 exclusion passes; seeding `_excluded` up front
  consumes one of those. Confirm the bound still holds for a 3-class fleet.
- Do not touch `count_recent_active_for_class` — #2698 may key new state off
  the same records, and two in-flight edits to that function would conflict.

