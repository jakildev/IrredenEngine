## Plan: fleet: dispatcher-assigned planning (retire the planning-claim contention machinery)

- **Issue:** #2197
- **Model:** opus — the design below is committed (no open judgment calls); implementation is bounded bash+python across the dispatcher fan-out loop with an existing test harness. Not sonnet: the dispatch loop's failure paths (claim-then-launch, error-path release, mode gating) are subtle enough to warrant opus.
- **Date:** 2026-07-07

### Scope

Make the dispatcher hand a **specific, already-claimed** needs-plan issue to the planning dispatch, so no two concurrent planning dispatches can target the same issue — by construction, because a planning dispatch only exists after a successful sole-holder claim. Workers stop self-selecting planning work entirely. The `fleet:planning-*` label lock survives as a primitive (cross-host arbiter, TTL orphan reaper, architect path), but the *contention protocol* — N worker panes racing per tick and arbitrating after the fact — is retired.

### Verified current state

Confirmed by code read on `origin/master` (256d24f9):

- **No per-item assignment exists anywhere in the dispatcher.** `build_dispatch_command` (`scripts/fleet/fleet-dispatcher:1182-1215`) emits exactly six args — `pane_key model effort role fallback mode` — and the wrap launches the generic `/role-worker live` prompt (`fleet-dispatch-wrap:178-183, 204`). The dispatcher header states the model outright: same command to every idle pane, `fleet-claim` arbitrates (`fleet-dispatcher:20-23, 1329-1335`).
- **Same-tick planning fan-out is already capped at ~1 pane per class.** `_candidates` de-dups needs_plan to one yield per planning class (`fleet_task_class.py:263-269`), `count` caps the pane fan-out (`fleet-dispatcher:1379-1414`). So the issue body's "every idle pane" framing is slightly stale: the *residual* contention is (a) **every dispatched opus+ iteration runs role step 2 before pickup** — a tick that fires 3 opus panes for 3 opus *tasks* still produces 3 contenders for the same oldest needs-plan issue; (b) **cross-tick** races beyond the 90s `CLAIM_SETTLE_SECONDS` window; (c) **cross-host** — each host runs its own dispatcher (mac + windows claims both live in the queue today) and nothing partitions issues between them.
- **The lock is a GitHub label, not a file.** `cmd_planning_claim` (`fleet-claim:1498-1547`) takes `fleet:planning-<host>-<agent>` via the shared lex-min `_acquire_label_on`/`_claim_decision` primitive; exit 0 = sole holder, 1 = lost, 2 = misuse (incl. `--replan` without `fleet:needs-plan` live), 3 = `## Plan` comment already exists. No heartbeat; orphans reaped only by the `cleanup --gh` fourth pass at `FLEET_CLAIM_STALE_SECS_PLANNING` = 3600s (`fleet-claim:1956-2003`), invoked from the scout on projection change (`fleet-state-scout:2701-2705`). Release removes only the caller's own host+agent label (`fleet-claim:1425-1449`) — re-claim by the same host+agent is idempotent (POST + sole-holder re-read → win).
- **The scout is planning-lock-blind.** `needs_plan[]` is the raw `fleet:needs-plan` set, number-sorted (`fleet-state-scout:406-435`); the worker projection/slice filter only `human:owned/wip/no-plan` (`_HUMAN_GATE_LABELS`, `fleet-state-scout:394, 1622-1623, 2104-2106`). Trigger is edge-fired on set-membership hash (`update_role_trigger`, `fleet-state-scout:2330-2373`) with a periodic safety re-arm (`fleet-dispatcher:1266-1269, 1709-1748`).
- **Sonnet light-plan routing is dispatch-side and label-authoritative**: `_plan_class` (`fleet_task_class.py:197-216`) → sonnet for `fleet:sonnet`-tagged issues, else fable degrading to opus when `count_active_for_class fable >= FLEET_CONCURRENCY_MODEL_FABLE` (default 1, **per host** — `fleet-dispatcher:166-169, 749-759`).
- Races the machinery exists for: #1810 (3 duplicate plans, same tick) → lex-min lock (#1978/#2035); #1999 (3 panes re-derived a stale plan) → `--replan`.

### Approach

**Assignment IS the claim.** The dispatcher takes the existing planning claim itself — under the **target pane's worktree basename** — *before* dispatching, then passes the assignment into the iteration. Contenders drop from N panes × M hosts to M dispatchers (plus the interactive architect), and a dispatcher that loses a cross-host race simply assigns the *next* issue — a lost race becomes parallel planning of distinct issues instead of a burned worker iteration.

Steps, in one PR:

1. **`fleet_task_class.py`: add a `--plan-pick` mode and a 5th output field.**
   - `python3 fleet_task_class.py --plan-pick <slice.json> <class> <fable-blocked 0|1>` prints ordered `repo:number` lines: the slice's `needs_plan[]` entries (already human-gate-filtered, engine-first/oldest-first) whose `_plan_class` == `<class>`.
   - `resolve()`'s output line gains a 5th token: `<class> <effort> <more> <count> <plan 0|1>` — 1 when the elected class's candidate set includes its (single, per-class-deduped) needs_plan yield. The per-class de-dup **stays**: one planning assignment per class per tick is a deliberate serialization (planning is not the throughput bottleneck — issue body, candidate 1); parallelism across classes (sonnet light-plan alongside a fable heavy plan) is preserved. Multi-issue fan-out within a class is an explicit non-goal.
   - Update the now-stale contention comment at `fleet_task_class.py:229-244`.
2. **`fleet-dispatcher`: claim-then-assign in `dispatch_role`.** When the elected class has `plan == 1`, `DISPATCH_MODE == live`, and a pane is about to be dispatched for that class, iterate `--plan-pick` lines for the **first** pane only:
   - `fleet-claim [--repo game] planning-claim <N> <pane-worktree-basename>` (basename via the existing `pane_current_path` helper; game namespace when the line's repo is `game`).
   - **exit 0** → assigned; stop.
   - **exit 3** (`## Plan` comment exists) → this is either already-planned-with-stale-slice or the re-plan state (plan-review bounce / stale-queued flip). Retry with `--replan`: **exit 0** → assigned as a re-plan; **exit 2** (no live `fleet:needs-plan`) → genuinely done, next line.
   - **exit 1** (cross-host dispatcher or architect holds it) → next line.
   - All lines exhausted → no assignment; subtract the planning candidate from the effective count, and if that leaves 0 claimable items of the elected class, skip the dispatch entirely (no no-op iteration).
   - Append a 7th arg `plan=<repo>:<N>` in `build_dispatch_command` for the assigned pane; stamp `plan_issue` into the pane's dispatch record (`write_dispatch_record`) for `fleet-debug` observability.
   - **Error-path release:** if the launch fails after a successful claim (tmux send error, occupied-pane skip), `planning-release` with the same basename before moving on.
3. **`fleet-dispatch-wrap`: accept the optional 7th arg**, export `FLEET_PLAN_ISSUE=<repo>:<N>` (unset when absent). No prompt change — the role doc reads the env var.
4. **Worker protocol (docs now, role docs when the human applies the gated edit):**
   - `FLEET_PLAN_ISSUE` set → verify the issue still carries `fleet:needs-plan` live (if not: `planning-release`, skip); read the thread; post `## Plan`; swap `fleet:needs-plan` → `fleet:plan-review` (+ `human:review-plan` when high-stakes); `planning-release`. **No `planning-claim` call** — the dispatch arrives pre-claimed under this pane's own agent name, so release Just Works.
   - `FLEET_PLAN_ISSUE` unset → **skip planning entirely.** This also deletes today's hidden tax where every task-elected opus+ iteration burns a claim attempt at step 2.
   - Sonnet class: identical mechanics; the light-plan flow (`fleet-plan-lint`, self-queue on pass) is unchanged past the claim step.
   - **Stale-queued-plan discovery simplifies:** the discoverer flips `fleet:queued` → `fleet:needs-plan`, comments why, and moves on — no lock, no inline re-derivation. The flip re-enters the issue into `needs_plan[]`, and the dispatcher's exit-3→`--replan` path routes the re-plan like any first plan. Workers never invoke `--replan` again.
5. **Docs in the same PR** (non-gated): rewrite PLANNING-PROTOCOL.md step 0 (claim → assignment), the light-plan step 1, and §"Re-planning a stale queued plan" (flip-and-move-on contract); update the planning-lock prose in `fleet-labels-reference.md` and the `fleet-state-machine.json` transition note. No label is added or removed, so no catalog change.
6. **File the gated follow-up** per TASK-FILING.md (no labels, `[no-plan]` token, noting it is gated self-config for the human to apply directly): rewrite `role-worker.md` step 2 / startup echo of `FLEET_PLAN_ISSUE`, and the `role-opus-reviewer.md:172` exit-3 note.

**Retire / keep inventory** (the migration analysis the issue asks for):

| Surface | Verdict |
|---|---|
| Worker-side `planning-claim` acquisition + exit-code protocol (role step 2, PLANNING-PROTOCOL step 0) | **Retired** — replaced by pre-claimed assignment |
| Worker-side `--replan` flow (flip + lock + inline re-derive) | **Retired** — flip-and-move-on; dispatcher routes the re-plan |
| `cmd_planning_claim` / `planning-release` / lex-min primitive (`fleet-claim`) | **Kept** — now called by the dispatcher (M contenders) + architect; unchanged code |
| Exit-3 `## Plan` dedup early-out | **Kept** — the dispatcher's stale-slice guard |
| `--replan` gating on live `fleet:needs-plan` | **Kept** — the dispatcher's re-plan-state discriminator |
| TTL sweep (3600s, `cleanup --gh` pass 4) | **Kept** — orphan reaper for died iterations, unchanged |
| `test_fleet_claim_planning.sh` | **Kept green** — primitive untouched |

Deleting `cmd_planning_claim` outright is out of scope: the architect's interactive planning path (`role-opus-architect`, human-cued) legitimately races the dispatcher and needs the same mutex.

### Interaction analysis

- **Sonnet light-plan lane (#2192, merged):** `_plan_class` is reused verbatim by `--plan-pick`, so a `fleet:sonnet`-tagged issue is assigned to a sonnet dispatch and every other issue to fable/opus. Cross-class fan-out (`exclude` re-resolve) still serves a sonnet light-plan concurrently with a heavy plan — each carries its own assignment.
- **Fable cap:** unchanged. `_plan_class`'s opus degrade on `fable_blocked` happens before assignment; the cap remains per-host (`count_active_for_class` reads local dispatch records) — a pre-existing property this design neither fixes nor worsens.
- **Cross-repo:** `--plan-pick` lines carry the repo namespace; the dispatcher adds `--repo game` to `fleet-claim` for game issues. Slice ordering (engine-first, oldest-first within repo) is inherited, preserving today's priority.
- **Cross-host:** the label claim is the shared coordination point (GitHub is the only cross-host state). Two dispatchers colliding on the oldest issue resolve by lex-min in one round; the loser assigns the next issue. No deterministic host partition is attempted — host liveness is dynamic and a down host would strand its partition.
- **Architect:** keeps calling `planning-claim` directly; mutual exclusion with dispatcher assignments via the same primitive.
- **API quota (Q1–Q4, #2219–#2222 in flight):** adds ≤3 REST calls per planning dispatch (claim, maybe `--replan` retry, rare release). At most one assignment per class per tick — negligible against the polling budget those tickets address; no overlap with their file surfaces (`fleet_gh_poll.py` / `fleet_poll_topology.py` contain no planning logic).
- **Sibling PRs:** #2284 (open, approved) touches `fleet-up`/`fleet-babysit`/`install.sh` — disjoint from this PR's `fleet-dispatcher`/`fleet-dispatch-wrap`/`fleet_task_class.py` surface. #2192/#2195 are merged and reflected above. In-flight ingest work (#2110, #2196) touches `fleet-queue-ingest` only — no overlap.

### One task or subtasks

**One implementation task (this issue, one PR)** — scripts + tests + non-gated docs land together so code and protocol can't drift. Plus the **gated follow-up issue** from step 6 (human-applied; not fleet-queueable — every worker class hits the self-config gate deterministically). Ordering constraint between them is a hard rule, see Gotchas.

### Affected files

- `scripts/fleet/fleet_task_class.py` — `--plan-pick` mode; 5th output token; stale comment refresh
- `scripts/fleet/fleet-dispatcher` — claim-then-assign loop in `dispatch_role`; 7th dispatch arg; `plan_issue` in dispatch records; error-path release; effective-count reduction on assignment failure
- `scripts/fleet/fleet-dispatch-wrap` — optional 7th arg → `FLEET_PLAN_ISSUE` export
- `scripts/fleet/tests/test_fleet_task_class.py` — `--plan-pick` cases (class routing, fable-degrade, repo ordering, human-gate filtering); 5-token output cases
- `scripts/fleet/tests/test_dispatcher_class_dispatch.sh` — extend: assignment attached to first pane of elected class; claim-fail fallthrough; plan-only-tick skip; live-mode gating
- `scripts/fleet/tests/test_dispatch_wrap_session.sh` — extend: `FLEET_PLAN_ISSUE` export present/absent
- `docs/agents/PLANNING-PROTOCOL.md` — step 0 → assignment; light-plan step 1; re-plan section rewrite
- `docs/agents/fleet-labels-reference.md`, `docs/agents/fleet-state-machine.json` — planning-lock prose/transition notes
- *(gated follow-up, human-applied)* `.claude/commands/role-worker.md`, `.claude/commands/role-opus-reviewer.md`

### Acceptance criteria

- Given a slice with ≥1 needs-plan issue routed to class C, idle-pane headroom, and live mode: exactly one class-C dispatch carries `FLEET_PLAN_ISSUE` naming a specific issue, and the `fleet:planning-<host>-<basename>` label is on that issue **before** the claude process launches.
- Claim-fail fallthrough works: a held oldest issue → the next issue is assigned; all held/planned → no assignment, and when planning was the elected class's only candidate, no dispatch fires.
- Exit-3 + live `fleet:needs-plan` → assigned via `--replan`; exit-3 + exit-2 on retry → skipped (already done).
- `dry-run` / `review-only` dispatches never pre-claim.
- A launch failure after a successful claim releases the label in the same tick.
- All existing dispatcher/task-class/claim tests stay green; `test_fleet_claim_planning.sh` unmodified.
- After the gated role-doc edit is applied: worker iterations make zero `planning-claim` calls; iterations without `FLEET_PLAN_ISSUE` do no planning work.

### Gotchas

- **Deployment order is load-bearing.** Scripts-first is safe: an old-protocol worker that self-selects the assigned issue re-claims idempotently (same host+agent → exit 0) and plans it — correct, one redundant call. Role-docs-first is a **planning outage**: workers skip step 2 waiting for an env var no dispatcher sets. Land + deploy the scripts PR (dispatcher changes need a fleet bounce — in-process scout/dispatcher code doesn't hot-reload) before the human applies the gated edit.
- **Claim under the pane's worktree basename, exactly.** `planning-release` recomputes `<host>-<agent>` from the caller; any other claim name (e.g. "dispatcher") makes the worker's release a no-op and every assignment leaks a 1-hour orphan.
- **TTL reap doesn't flip the trigger hash** (label changes don't change needs-plan set membership). Re-dispatch after an orphan reap rides the periodic worker re-arm — existing behavior, but worth a test note so nobody "fixes" it into a hash input.
- **The assigned issue can go stale between claim and read** (human closes it, architect plans it out-of-band). The worker's live `fleet:needs-plan` re-verify + release covers this; don't skip it.
- **Engine-public wording** applies to the protocol docs — no game-side jargon in examples.

