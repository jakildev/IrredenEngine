# Epic-Steward: a durable epic-lifecycle role for the fleet

## Context

Epics are well-served at filing time (`file-epic`: umbrella + phased children +
repo-committed plans + `fleet-validate-stack`) but nobody owns them afterward.
Investigation confirmed the gaps:

- **Epic auto-close does not exist.** Three doc surfaces claim it does
  (`fleet-labels-reference.md`, `fleet-labels` line 105, `fleet-state-machine.json`)
  naming two *different* owners — but `fleet:epic` appears in the scout only in
  `_INGEST_SKIP_LABELS`. Closure has always been manual.
- **Design blocks are manual one-at-a-time architect work** (see commit 24d50b92:
  hand-written plan files, one docs PR, per-PR label flips). No aggregation, no
  completion tracking.
- **Plans go stale**: re-plans live in PR comment threads, not plan files; nobody
  re-validates a downstream child's plan after a sibling merges or the merger
  restacks it.
- **Scope changes are invisible**: issues filed mid-epic with `**Part of epic:** #N`
  are never adopted into the umbrella checklist; umbrella checklists have already
  drifted (epic #1553 has no body checklist at all — children live only in a comment).

The fix is a new autonomous role, **epic-steward**: transient, dispatcher-driven
(like opus-worker, not the interactive architect), one pane, Opus/heavy tier,
covering both repos. Durable state lives cross-host in GitHub (labels, umbrella
body checklist) + repo-committed plan files — never in a session.

**Decisions made with the user:**
1. Merger keeps mechanical restacking; steward does the *semantic* follow-up
   (ledger, plan re-validation, amendments).
2. Steward resolves design blocks autonomously **when derivable** from the umbrella
   plan/decision log; novel decisions aggregate into one proposal package for the
   human/architect.
3. Transient dispatcher-driven runtime; repo-committed ledger as memory.
4. **Delivery: file as a fleet epic** (dogfood — this becomes the first epic the
   steward shepherds once early phases land).

## The design

### Role responsibilities (four flows, priority order per epic)

a. **Design-block triage** — for `fleet:design-blocked` PRs whose backing issue is
   in a claimed epic's checklist: classify each question DERIVABLE (can cite the
   deciding sentence in the umbrella plan/decision log/design doc) vs NOVEL.
   All-derivable → amend the child plan (`## Amendments` entry), post
   `## Steward direction` PR comment, atomic `design-unblock` transition.
   Any-novel → swap PR label to `fleet:design-proposed`, post ONE aggregated
   `## STEWARD PROPOSAL` comment on the umbrella, add `fleet:steward-proposal`
   to the umbrella. Human/architect answers on the umbrella thread and removes
   the label → projection re-fires → questions are now derivable → steward
   distributes and unblocks. Non-epic design blocks remain the architect's lane.
b. **Post-merge follow-up** — child merged: tick `- [x] #N` in the umbrella body,
   update ledger, audit scope drift, re-validate every downstream child's plan
   against what actually merged (stale = references a symbol/file/decision the
   merge renamed/removed/superseded), amend stale plans. Never touches branches.
c. **Adoption** — open issues matching `Part of epic: #U` absent from the
   checklist: validate structured fields, append `- [ ] #K` to the checklist,
   plan stub if missing, re-run `fleet-validate-stack`.
d. **Close-out** — all checklist children closed: verify each closed via merged PR
   or explicit rationale, audit umbrella closing criteria with evidence, post
   closure-summary comment, close the umbrella. (Implements the promise the docs
   already make.)

Iteration budget: ≤2 epics, ≤3 triaged PRs, ≤1 proposal package per iteration;
all plan/ledger writes batch into **one docs-only PR per repo per iteration**
(branch `claude/epic-steward-<timestamp>`, via commit-and-push). Urgent signals
don't wait on the PR: unblocks signal via label flip + PR comment, proposals via
umbrella comment, close-outs via issue close.

### Durable state

- **Membership source of truth**: umbrella issue body `## Children` checklist
  (`- [ ] #N` / `- [x] #N`). Steward heals it on first claim of pre-protocol
  epics (union of checklist + `Part of epic` search + summary-comment table).
  Umbrella-body editing is an explicit steward carve-out, claimed umbrellas only.
- **Semantic ledger**: a `## Steward ledger` section appended to the umbrella's
  own plan file `.fleet/plans/issue-<U>.md` (NOT a separate file — worker role
  docs hard-rule `issue-<N>.md` as the only valid filename). Schema:
  `reconciled-through:` marker, `### Children` table (state/PR/plan/last-validated),
  `### Decisions` index, `### Events` log. `reconciled-through` makes fresh-context
  iterations idempotent.
- **Plan amendments**: append-only `## Amendments` section
  (`### A1 — <date> — trigger: <event>` + decision/supersedes/criteria/by),
  formalizing the existing ad-hoc "Architect RE-plan v3" pattern in issue-1457.md.

### New labels (exactly two, each load-bearing)

- `fleet:design-proposed` (PR scope) — steward queued this PR's question into a
  proposal; removes the PR from the steward's design projection edge-cleanly,
  joins `PARKED_PR_LABELS`, review-skip, merger-skip, and reconcile R2/R7
  exclusions (without which reconcile auto-"heals" every proposal within ~3 ticks).
- `fleet:steward-proposal` (umbrella issue scope) — proposal package pending;
  human-facing queue; its **removal** is the edge that re-fires distribution.

Transitions: new `design-propose` edge; `design-unblock` and `design-block` each
gain `fleet:design-proposed` in their remove sets. Catalog + state-machine JSON +
labels-reference must change in the same commit (`fleet-labels --check`).

### Claims (multi-device safety)

New `fleet:stewarding-<host>-<agent>` label class on the **umbrella issue** —
`_cmd_pr_label_claim` is already prefix-parameterized and works on issues
(fleet-claim:1196-1255). Per-epic granularity; same sole-holder tie-break as
`fleet:reviewing-`. New `cmd_cleanup_gh` pass sweeping open `fleet:epic` issues,
TTL `FLEET_CLAIM_STALE_SECS_STEWARD=3600`. Rejected: reusing `fleet:claim-*`
(issue-lifecycle semantics mismatch a short stewardship session).

### Scout projection (`project_epic_steward`)

New `fetch_epics(repo)` (one `gh` list call per repo per tick, body parsed into
`checklist` then popped); parse `Part of epic` where bodies already flow
(`fetch_task_queue`, `resolve_human_approved_blockers`). Ops, all edge-triggered
by the steward's own writes (no timestamps/transient labels in the hash — the
documented `project_merger` lesson):

- `normalize` — managed epic (plan file exists) with no body checklist → heal.
  Legacy checklist-less epics *without* plan files emit nothing (no perma-trigger).
- `rollup` — unchecked checklist child that is closed.
- `adopt` — visible open issue declaring `Part of epic: #U` absent from checklist.
- `design` — `fleet:design-blocked` PR branch-matched to a checklist child; plus
  `fleet:design-proposed` PRs whose umbrella no longer carries
  `fleet:steward-proposal` (= answered → distribute).
- `closeout` — all checklist children closed, umbrella open.

Closed-check uses already-fetched open/closed sets with rare
`_resolve_ref_satisfied` fallback. Membership join beyond the checklist happens
steward-side at iteration time (search is too expensive for the 30s tick).

### Boundary contracts

- **Merger** owns branches/re-targeting; steward never pushes to a child PR branch
  and only touches the design label pair via `fleet-transition`. Role-doc skip:
  defer plan re-validation while the next child's PR carries
  `fleet:merger-cooldown`/`fleet:stacked-rebase`.
- **Ingest** owns `fleet:queued`/`fleet:blocked`/model labels; steward's queue
  hygiene = validate-stack + fixing the three machine-parsed body lines only.
- **Architect** keeps non-epic design blocks + answers proposal packages
  (architect-protocol.md gains a "steward-first for epic children" paragraph).
- **Isolation**: game-epic ledgers/amendments commit only to the game repo;
  engine artifacts reference downstream work as `game#N`, no feature names; the
  canonical protocol says "downstream repo" throughout.

### Cross-repo inheritance (the connection improvement)

- New `docs/design/role-sharing.md` mirroring skill-sharing.md: canonical
  `docs/agents/<role>-protocol.md` declares a `## Repo deltas this flow needs`
  table; per-repo `.claude/commands/role-<name>.md` thin wrappers answer every
  key. Baseline key set: repo-slug, repo-root, worktree-path, role-name,
  role-banner, claim-tool-flags, feedback-file.
- **New-role checklist**: a PR adding a canonical protocol must add the engine
  wrapper in the same PR and file the downstream-wrapper follow-up issue on the
  game repo before merge.
- **New lint `fleet-validate-roles`** (pure file I/O, module + executable split
  like fleet-validate-stack): every protocol with a delta table has a wrapper in
  each fleet-enabled repo root; every wrapper answers every declared key. Alias
  map for role-game-architect's legacy key names initially (defer rename).

## The epic to file (execution = run `file-epic`)

Umbrella: `fleet: epic-steward — autonomous epic-lifecycle role (adopt / follow-up / design-triage / close-out)`

| Phase | Child | Model | Blocked by |
|---|---|---|---|
| P1 | docs: epic-steward canonical protocol, role-sharing.md, engine role wrapper, label catalog + transitions + doc fixes | opus | (none) |
| P2 | fleet: steward claim class + cleanup pass + design-proposed parked/reconcile exclusions | opus | P1 |
| P3 | fleet: scout epic fetch + project_epic_steward/slice + skip-set updates | opus | P1 |
| P4 | fleet: dispatcher + fleet-up registration (pane, caps, worktrees, bootstrap trigger, conf sample) | sonnet | P3 |
| P5 | fleet: `fleet-epic-status <N>` helper + install.sh | sonnet | P1 |
| P6 | fleet: `fleet-validate-roles` lint + tests (+ validate-stack `--check-checklist`) | sonnet | P1 |
| P7 | skills: file-epic initializes Children checklist + Steward ledger; anti-patterns | sonnet | P1 |

Plus: game-wrapper follow-up issue filed on `jakildev/irreden` per the new-role
checklist (the P1 PR files it — eating the dogfood).

Burn-in: role ships opt-in (`FLEET_EPIC_STEWARD=1` in fleet-up.conf), flipped to
default-on after a week of feedback entries. Heal-on-first-claim backfill only;
no bulk backfill pass. Budget numbers (2/3/1) are tunable defaults.

### Per-phase content anchors (verified)

- **P1**: new `docs/agents/epic-steward-protocol.md` (full skeleton drafted in
  planning — startup, claim etiquette, four flows, amendment format, proposal
  format, escalation rules, modes, hard rules); new `docs/design/role-sharing.md`;
  new `.claude/commands/role-epic-steward.md` (Deltas: repo-slug, downstream-repo
  pair, worktree paths, escalation-target=opus-architect, ledger-branch-prefix
  `claude/epic-steward-`, feedback file); edits to
  `docs/agents/architect-protocol.md` (steward-first), `docs/agents/FLEET.md`
  (design-escalation branch), `docs/agents/fleet-labels-reference.md` (fix
  `fleet:epic` auto-close claim → steward; register both new labels),
  `scripts/fleet/fleet-labels` (~line 99 LABELS + fix line 105 epic description),
  `docs/agents/fleet-state-machine.json` (nodes + `design-propose` edge + edits
  to `design-unblock`/`design-block` remove-sets).
- **P2**: `scripts/fleet/fleet-claim` — `cmd_steward_claim/release` next to line
  1250 wrappers; new cleanup pass after line ~1472 (open `fleet:epic` issues);
  `scripts/fleet/fleet_branch_match.py:70` PARKED_PR_LABELS += design-proposed;
  reconcile R2 (~2133), R7 (~2176), R4a (~2069) exclusions. Tests: clone of
  test_fleet_claim_acquire + reconcile-heal regression.
- **P3**: `scripts/fleet/fleet-state-scout` — `fetch_epics` after line 199 +
  fetch_specs (~1601-1617); `_parse_epic_checklist` regex (first `#N` per
  checkbox line, handles bold-wrapped refs); epic field in task/approved dicts;
  `project_epic_steward` + `PROJECTORS` (1379), `slice_epic_steward` + `SLICERS`
  (~1562); `fleet:design-proposed` added to REVIEW_SKIP_LABELS (782),
  `_DESIGN_BLOCK_LABELS` (851), `_merger_action_signal` (1248); epics in detail
  cache + gc keep-set. Tests: projection fixtures (quiescence, edge-consumption,
  legacy-epic-emits-nothing), checklist parser.
- **P4**: `scripts/fleet/fleet-dispatcher` — DISPATCHED_ROLES (285), env capture,
  ROLE_CONCURRENCY=1 (105), ROLE_MODEL=$HEAVY_MODEL (318), ROLE_EFFORT=xhigh
  (335); `scripts/fleet/fleet-up` — dual worktrees (engine + game, gated on
  FLEET_EPIC_STEWARD), reset_worktree, settings loop, ops-window pane after
  smoke-worker block (~1382), bootstrap trigger predicate (~1042);
  fleet-up.conf.sample. No fleet-dispatch-wrap change needed.
- **P5**: new `scripts/fleet/fleet-epic-status` (umbrella state, checklist-vs-
  discovered drift rows, per-child state/PR/plan presence, embedded
  validate_stack; `--json` for steward consumption); install.sh pairs.
- **P6**: new `scripts/fleet/fleet_validate_roles.py` + executable + tests.
- **P7**: `docs/agents/skills/file-epic.md` — step 2 seeds ledger section, new
  step 3.5 writes the `## Children` body checklist, step 7 links ledger,
  anti-pattern entry. Wrappers inherit automatically; no new delta keys.

## Verification

- After P1: `fleet-labels --check` passes; `fleet-transition design-propose <PR>`
  dry-runs against the JSON; engine wrapper resolves the protocol path.
- After P2: `scripts/fleet/tests/test_fleet_claim_steward.sh` green; reconcile
  regression test proves a design-proposed PR is NOT healed.
- After P3: projection tests green (same-state-twice → same hash; box-check
  consumes rollup; checklist-less legacy epic emits nothing); run scout one tick
  locally and inspect `~/.fleet/state/projections/epic-steward.json`.
- After P4: `FLEET_EPIC_STEWARD=1 fleet-up` creates the pane; seed a trigger and
  watch one dispatched iteration end-to-end on a test epic.
- After P6: `fleet-validate-roles` passes on the tree and fails when a delta key
  is deleted from the engine wrapper (negative test).
- End-to-end dogfood: this epic itself — once P1-P4 land, the steward's first
  live claims should be on *this* umbrella (rollup ticks as P5-P7 merge, and the
  close-out flow closes it).

---

## Steward ledger

- **reconciled-through:** 2026-06-10 (filed)
- **proposal pending:** none

### Children

| Child | Phase | State | PR | Plan | Last validated |
|---|---|---|---|---|---|
| #1662 | P1 | open | — | ok | 2026-06-10 (filed) |
| #1663 | P2 | open | — | ok | 2026-06-10 (filed) |
| #1664 | P3 | open | — | ok | 2026-06-10 (filed) |
| #1665 | P4 | open | — | ok | 2026-06-10 (filed) |
| #1666 | P5 | open | — | ok | 2026-06-10 (filed) |
| #1667 | P6 | open | — | ok | 2026-06-10 (filed) |
| #1668 | P7 | open | — | ok | 2026-06-10 (filed) |

### Decisions

- Merger keeps mechanical restacking; the steward does semantic follow-up only (user decision, 2026-06-09).
- Steward resolves design blocks autonomously when derivable from the umbrella plan/decision log; novel questions aggregate into one proposal package (user decision, 2026-06-09).
- Transient dispatcher-driven runtime; durable state in GitHub + repo-committed plan files (user decision, 2026-06-09).
- Exactly two new labels: `fleet:design-proposed` (PR scope) + `fleet:steward-proposal` (issue scope).
- Ledger lives inside the umbrella's `issue-<N>.md` plan file — a separate filename would violate the worker role docs' plan-filename hard rule.
- Role ships opt-in behind `FLEET_EPIC_STEWARD=1`; default-on flip decided after a week of feedback entries.

### Events

- 2026-06-10 — filed via file-epic; children #1662–#1668; ledger seeded manually (pre-dates P7 #1668, which automates this).
