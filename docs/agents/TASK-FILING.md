# TASK-FILING.md — filing issues into the fleet queue

How fleet roles file work as GitHub issues. Label semantics and the state
machine: [`fleet-labels-reference.md`](fleet-labels-reference.md).

## Single issue

File with **no labels**:

```
gh issue create --repo jakildev/IrredenEngine --title "<short title>" --body "<body>"
```

(`--repo jakildev/irreden` for game-side work.)

State labels (`fleet:task`, `fleet:queued`, `fleet:needs-plan`,
`fleet:opus` / `fleet:sonnet`, …) belong to reviewers, the human, and the
scout's ingest: the human stamps `human:approved` and the scout adds the
rest. The one filer-owned exception is the agent-approved follow-up lane
below.

The body carries these standalone lines (the scout's ingest and
`fleet-claim`'s blocker gate parse them):

- **Area:** e.g. `engine/render`, `engine/math`, `docs`
- **Model:** `opus` or `sonnet`
- **Blocked by:** `(none)` or `#NNN`
- **Acceptance criteria** — the definition of done: one line per criterion,
  each naming the validator that proves it ([`VALIDATION.md`](VALIDATION.md))
  and the reading it must show
- **Context** — what you observed and why it matters

Optional:

- **Objective:** `<slug>` — the objective file's basename under
  [`docs/design/objectives/`](../design/objectives/README.md); read by the
  objectives sweep and the human, not by a parser.
- **Host:** `linux` | `windows` | `macos` — for work that runs on one OS
  only (a `linux-debug` reference bless, a Windows DLL-staging check, a
  Metal-only capture). The scout projects it as `needs_host` and the
  dispatcher never elects the task on another host; `fleet-claim` does not
  enforce it, so a hand-picked claim on the wrong host is refused only by
  the pane's own read of the body. Finer than `fleet:needs-gl-host`, which
  lets linux and windows stand in for each other.

### File with a plan

When the task was planned with the human, or is substantial enough to need
a plan, post the `## Plan` comment at file time. The planning gate in
`fleet-queue-ingest` keys on a `## Plan` **comment** — a plan left in the
body bounces the issue to `fleet:needs-plan` and a worker re-plans it. With
the comment, the issue queues directly:

```
gh issue create --repo jakildev/IrredenEngine --title "<short title>" --body "<body>"
gh issue comment <N> --repo jakildev/IrredenEngine --body "## Plan
<structured plan per PLANNING-PROTOCOL.md step 2>"
```

The comment follows [`PLANNING-PROTOCOL.md § The flow`](PLANNING-PROTOCOL.md)
step 2 (first heading starts with `## Plan`). Planned with the human → post
`## Plan`; mechanical → no plan, ingest bounces it to `fleet:needs-plan` for
a worker to plan and a reviewer to vet; trivial → the human's
`human:no-plan` label or a `[no-plan]` tag.

### Agent-approved follow-up lane (no human triage)

A fleet role filing a **follow-up for defect-shaped work it verified
itself** may enter the queue without `human:approved`. All of these hold,
or the issue files unlabeled:

- **You verified the finding this session** — a repro you ran, output you
  observed, a source read you performed. A hunch or a feature idea is not
  eligible.
- **Fleet-infrastructure findings need a fired incident, not a structural
  read.** When the fix surface is `scripts/fleet/`, the `docs/agents/`
  protocol docs, CI glue, or the `fleet-*` tools and tests, the misbehavior
  must have fired — a live incident, or an execution you performed that
  demonstrates it end-to-end. A defect established by reading source files
  unlabeled.
- **The work is defect-shaped** — wrong, stale, missing, drifting. New
  capabilities, public-API additions, and design-direction changes are not.
- **Not a routed-elsewhere class:** coding-improvement observations
  (`fleet:coding-improvement`), architectural questions
  (`fleet:design-blocked` on the PR), multi-issue stacks (`file-epic`),
  gated self-config (role docs, skills).
- **No existing open issue covers it** — comment the new occurrence there
  instead.

**Mechanics.** The standard body with fix-forward-grade forensics (repro
command, observed output, suspected window, what was ruled out), plus
`--label "fleet:agent-approved"`, plus exactly one of three plan shapes:

1. **Bounded one-session fix** → also `--label "fleet:no-plan"` (the
   agent-applied twin of `human:no-plan`): single module, no design choice,
   runnable acceptance criteria. Ingest queues it directly; the default for
   most follow-ups.
2. **You know the fix and it has structure** → post a `## Plan` comment
   (PLANNING-PROTOCOL.md step 2) at file time and add
   `--label "fleet:plan-review"`; the plan reviewer vets it like any other.
   An ingest tick landing before the comment stamps `fleet:needs-plan` on
   top; ingest reconciles the pair on its next tick — no hand-stripping.
3. **You verified the defect but not the fix** → add neither; ingest
   bounces it to `fleet:needs-plan`.

`fleet:agent-approved` is never removed — it is the audit trail. The
human's veto is ordinary mechanics: close, or park (`human:owned`,
`fleet:needs-human`). Same-PR and immediate-sibling-PR fix-forward remain
preferred (FLEET.md §"Fix-forward"); this lane is for the residual "file an
issue" case.

### Escalation issues (scope-grew)

A worker that hits a non-architectural blocker (scope grew, structural
build break, multi-module public-API surface) files a single issue with the
same body shape, prefixed with the escalation context:

```
gh issue create --repo jakildev/IrredenEngine --title "<what needs attention>" \
  --body "Escalated from <class>-class worker (scope grew).

**Area:** ...
**Model:** opus
**Blocked by:** (none)

Context: ..."
```

Then comment on the PR linking the issue, release the claim, reset, move
on. If the blocker meets the agent-approved bar, file through that lane.

A task that is merely **subtler than its class** (not bigger) is re-tagged
one class up on the same issue (`fleet:sonnet` → `fleet:opus` →
`fleet:fable`) and released — `role-worker.md` step 8a. Architectural
blockers go through `fleet:design-blocked` on the open PR, not a fresh
issue.

## Multi-issue stacks (epic decomposition)

A stack of N issues that each depend on the prior is filed with the
**`file-epic`** skill, never by hand:

```
/file-epic <path-to-approved-plan>
```

It emits an umbrella `fleet:epic`, one `fleet:task` child per phase, a
per-child `## Plan` comment, and a standalone `**Blocked by:** #<prior>`
chain — the only form the scout's `blocked_by` parser and `fleet-claim`'s
`find-stackable-blockers` read. Prose forms ("blocked on T1 + PR #N") are
not parsed: the child projects as Available and the chain never stacks.

If you must hand-file a stack, each child carries these standalone lines
immediately under the header bullet, before `## Scope`:

```
**Blocked by:** #<prior> (<one-line rationale>)
**Model:** <opus|sonnet>
```

- **One `#N` per `**Blocked by:**` line.** A multi-blocker line
  (`#1299, #1300`) does not stack-claim; the child projects as blocked
  until an upstream merges and the satisfied ref is stripped.
- **Prefer issue numbers as blockers.** `fleet-claim` treats a `#N` ref as
  gate-blocked until closed (issues close when their PR merges). A PR
  number resolves too, but use it only when the blocker has no backing
  issue.

Filed correctly, the cascade is automatic: T1 claims plain; the scout
enriches T2 with `stackable_blocker_pr`; the next worker claims T2
`--stackable-on <T1-PR>` and branches off T1's head; the merger re-targets
each child onto master as upstreams merge.
