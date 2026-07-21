# TASK-FILING.md — filing issues into the fleet queue

How fleet roles file new work as GitHub issues. Used by the architect
(files work it identifies) and the workers (file follow-ups +
escalations). All point here rather than restating the convention.

The label state machine these issues flow through lives in
[`FLEET.md § Issue/PR labeling discipline`](FLEET.md). This doc covers
the filing mechanics only.

---

## Single issue

File with **no labels**:

```
gh issue create --repo jakildev/IrredenEngine --title "<short title>" --body "<body>"
```

(For game-side work: `--repo jakildev/irreden`.)

Do NOT pre-apply `fleet:task`, `fleet:queued`, `fleet:needs-plan`,
`fleet:opus` / `fleet:sonnet`, or any other state label. State labels
are owned by specific roles (reviewers, the human) and by the scout's
triage flow. **Author-side filing adds zero state labels** and lets the
human stamp `human:approved` when they want it picked up; the scout
ingests it on its next pass and stamps the rest. The exception is the
**agent-approved follow-up lane** below, whose labels
(`fleet:agent-approved`, `fleet:no-plan`, `fleet:plan-review`-with-a-plan)
are deliberately filer-owned — use it when the follow-up qualifies.

The body should include these standalone lines (the scout's queue-ingest
and `fleet-claim`'s blocker gate parse them):

- **Area:** e.g. `engine/render`, `engine/math`, `docs`
- **Model:** `opus` or `sonnet`
- **Blocked by:** `(none)` or `#NNN`
- **Acceptance criteria** — concrete check (build passes, test X works)
- **Context** — why this matters, what you observed

Optional, when the work serves a standing objective
([`docs/design/objectives/`](../design/objectives/README.md)):

- **Objective:** `<slug>` — the objective file's basename. No parser
  reads it; the architect's objectives sweep and the human use it to
  attribute shipped work to the objective's progress ledger.

The issue sits in the backlog until the **human triages and adds
`human:approved`**. Only then does the scout ingest it.

### File with a plan (the architect default for substantial tasks)

When you (the architect) **planned a task with the human** in a design
conversation, or the task is substantial enough to need a plan, **post the
structured `## Plan` comment at file time** — don't leave the plan in the issue
body. The planning gate in `fleet-queue-ingest` keys on a `## Plan` *comment*
(per #1932), not the body, so an issue whose plan lives only in the body gets
bounced to `fleet:needs-plan` and a worker **re-plans work the human already
shaped** (and may loop back to the human for plan review) — a wasted pass.
Posting the `## Plan` comment makes the issue **queue directly**, skipping
`fleet:needs-plan` entirely (the human was already in the planning loop, so no
further sign-off is needed):

```
gh issue create --repo jakildev/IrredenEngine --title "<short title>" --body "<body>"
gh issue comment <N> --repo jakildev/IrredenEngine --body "## Plan
<structured plan per PLANNING-PROTOCOL.md §2 — Scope / Approach / Affected files /
Acceptance criteria / Gotchas>"
```

The `## Plan` comment must follow the structure in
[`PLANNING-PROTOCOL.md § The flow`](PLANNING-PROTOCOL.md) (its first heading
starts with `## Plan` so the gate finds it). Today only the `file-epic` skill
posts `## Plan` comments (for epic children); this generalizes the same
mechanism to single tasks.

**Plan-less filing is still valid** for mechanical or obvious tasks: file with
no `## Plan` comment and the ingest bounces it to `fleet:needs-plan` for a
worker to plan (the safety net). For a genuinely trivial change, the human can
opt out at filing with `human:no-plan` / a `[no-plan]` tag and it queues with no
plan at all. The choice: planned-with-the-human → post `## Plan` (queues
directly); mechanical → leave it (worker plans, and if **high-stakes** the
worker holds it on `human:review-plan` for your approach sign-off — see
[`PLANNING-PROTOCOL.md § The flow`](PLANNING-PROTOCOL.md) step 3); trivial →
`human:no-plan`.

### Agent-approved follow-up lane (no human triage)

When a fleet role files a **follow-up for defect-shaped work it verified
itself** — a crash it reproduced, a stale reference it confirmed, a parity
gap it measured, dead code it traced — the issue can enter the queue
**without waiting for `human:approved`**. The human's trust here is
standing, not per-issue; their touchpoints move to PR merge time and the
audit trail (`gh issue list --label fleet:agent-approved`).

**Eligibility.** All of these must hold, or the issue files unlabeled for
human triage as before:

- **You verified the finding this session** — a repro you ran, output you
  observed, a source read you performed. A hunch, a "probably", or a
  feature idea is not eligible; those are the human's to shape.
- **The work is defect-shaped**: fixing something that is wrong, stale,
  missing, or drifting. New capabilities, public-API additions, and
  design-direction changes are not.
- **It is not one of the routed-elsewhere classes**: coding-improvement
  observations (`fleet:coding-improvement`, human-cued by design),
  architectural questions (`fleet:design-blocked` on the PR),
  multi-issue stacks (`file-epic`, human-approved children), or work
  whose fix surface is gated self-config (role docs, skills — the fleet
  can't edit those anyway).
- **You searched for an existing open issue first** and found none
  covering the same defect (comment the new occurrence there instead of
  filing a duplicate).

**Mechanics.** File with the standard body (Area / Model / Blocked by /
Acceptance criteria / Context, with fix-forward-grade forensics: repro
command, observed output, suspected window, what was ruled out), plus
`--label "fleet:agent-approved"`, plus exactly one of three plan shapes:

1. **Bounded one-session fix** → also add `--label "fleet:no-plan"` (the
   agent-applied twin of `human:no-plan`). Bar: single module, no design
   choice to make, acceptance criteria runnable — a worker can
   investigate and fix it in one session. Ingest queues it directly;
   the worker opens a code-only PR. This is the expected default for
   most follow-ups.
2. **You know the fix and it has structure** → post a `## Plan` comment
   (per [`PLANNING-PROTOCOL.md § The flow`](PLANNING-PROTOCOL.md) step 2)
   at file time AND add `--label "fleet:plan-review"`. You already have
   the root cause in context — write it down instead of making a planner
   re-derive it. The plan reviewer vets your plan like any other
   (sound → clears the label, queues; unsound → bounces to
   `fleet:needs-plan`). Apply the step-3 high-stakes checklist yourself:
   if any item trips, also add `human:review-plan` — that human approach
   gate is the one pre-merge human touch this lane keeps.
3. **You verified the defect but not the fix** → add neither. Ingest
   bounces it to `fleet:needs-plan` and the autonomous planning lane
   takes it from there.

`fleet:agent-approved` is **never removed** — it is the permanent record
that the issue entered the queue on agent judgment. The human's veto is
ordinary label mechanics: close the issue, or park it (`human:owned`,
`fleet:needs-human`).

The fix-forward hierarchy is unchanged: same-PR and immediate-sibling-PR
remain the preferred vehicles (FLEET.md §"Fix-forward"). This lane makes
the *exception* case — "file an issue" — stop dying in the triage
backlog; it does not make filing issues the default again.

### Escalation issues (scope-grew)

When a worker hits a non-architectural blocker (scope grew, structural
build break, multi-module public-API surface), file the follow-up as a
single issue with the same body shape, prefixed with the escalation
context:

```
gh issue create --repo jakildev/IrredenEngine --title "<what needs attention>" \
  --body "Escalated from <class>-class worker (scope grew).

**Area:** ...
**Model:** opus
**Blocked by:** (none)

Context: ..."
```

Then comment on your PR linking the filed issue, release the claim,
reset, and move on. When the escalation meets the agent-approved lane's
eligibility bar (you verified the blocker yourself and the residual work
is defect-shaped), file it through that lane so it re-queues without
human triage; otherwise the human triages and stamps `human:approved`.

> A task that is merely **subtler than its class** (not bigger) does
> NOT get a fresh issue — re-tag the same issue one class up
> (`fleet:sonnet` → `fleet:opus` → `fleet:fable`) and release, per
> `role-worker.md` step 8a.

> Architectural blockers route differently — via the
> `fleet:design-blocked` label on the open PR, not a fresh issue. See
> the worker / architect role files for the design-escalation flow.

---

## Multi-issue stacks (epic decomposition)

When work decomposes into a **stack of N issues that each depend on the
prior** (the canonical smooth-yaw / SO(3) / rotation / streaming
patterns), do NOT hand-file the children. Invoke the **`file-epic`**
skill instead:

```
/file-epic <path-to-approved-plan>
```

**Why it matters.** The scout's `blocked_by` parser and `fleet-claim`'s
`find-stackable-blockers` predicate both read a **standalone**
`**Blocked by:** #N` line in the issue body. Prose forms buried in a
header bullet ("Blocked on T1 + docs PR #1306") are NOT parsed — the
child projects as Available, no `--stackable-on` claim fires, and the
chain doesn't stack. Hand-filing reliably produces this drift.
`file-epic` enforces the template (umbrella `fleet:epic` + one
`fleet:task` child per phase + per-ticket plan files + a standalone
`**Blocked by:** #<prior>` chain).

**If you must hand-file a stack** (one-off, plan not yet written), each
child MUST carry these as standalone lines, placed immediately under the
header/epic bullet and before `## Scope`:

```
**Blocked by:** #<prior> (<one-line rationale>)
**Model:** <opus|sonnet>
```

Rules that make the stack actually stack:

- **One `#N` per `**Blocked by:**` line.** Multi-blocker forms
  (`**Blocked by:** #1299, #1300`) do NOT stack-claim under the current
  implementation — the scout only enriches single-blocker tasks with
  `stackable_blocker_pr`. A multi-blocker child projects as "blocked"
  and is skipped until at least one upstream merges and you strip the
  satisfied ref. (Live multi-blocker resolution is planned but not
  landed — track via the open scout/fleet-claim blocker-resolution
  issue.)
- **Use issue numbers as blockers; PR numbers are unreliable.**
  `fleet-claim` treats a `#N` ref as gate-blocked until CLOSED (issues
  close when their PR merges), whereas a PR number left as blocker adds
  ambiguity. To gate on a docs PR that has no backing issue, withhold
  `human:approved` on the dependent task instead.

Once filed correctly, the cascade is automatic: T1 claims plain and
opens `claude/<T1>-*`; the scout enriches T2 with `stackable_blocker_pr`
pointing at T1's PR; the next worker claims T2 `--stackable-on <T1-PR>`
and branches off T1's head; and so on. The merger re-targets each
child's base onto master as upstreams merge.
