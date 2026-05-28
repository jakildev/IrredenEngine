# TASK-FILING.md — filing issues into the fleet queue

How fleet roles file new work as GitHub issues. Used by the architect
(files work it identifies), opus-worker (files follow-ups + escalations),
and sonnet-author (files opus escalations). All point here rather than
restating the convention.

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
triage flow. **Author-side filing adds zero labels** and lets the human
stamp `human:approved` when they want it picked up; the scout ingests it
on its next pass and stamps the rest.

The body should include these standalone lines (the scout's queue-ingest
and `fleet-claim`'s blocker gate parse them):

- **Area:** e.g. `engine/render`, `engine/math`, `docs`
- **Model:** `opus` or `sonnet`
- **Blocked by:** `(none)` or `#NNN`
- **Acceptance criteria** — concrete check (build passes, test X works)
- **Context** — why this matters, what you observed

The issue sits in the backlog until the **human triages and adds
`human:approved`**. Only then does the scout ingest it.

### Escalation issues (sonnet → opus, or scope-grew)

When a `[sonnet]` task turns out to need Opus, or a worker hits a
non-architectural blocker (scope grew, structural build break,
multi-module public-API surface), file the follow-up as a single issue
with the same body shape, prefixed with the escalation context:

```
gh issue create --repo jakildev/IrredenEngine --title "<what needs opus attention>" \
  --body "Escalated from sonnet.

**Area:** ...
**Model:** opus
**Blocked by:** (none)

Context: ..."
```

Then comment on your PR linking the filed issue, release the claim,
reset, and move on. The human triages and stamps `human:approved`.

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
- **The blocker must be an issue number, not a PR number.**
  `fleet-claim`'s blocker gate resolves `#N` via `gh issue view --json
  state` (CLOSED = satisfied). To gate on a docs PR that has no backing
  issue, withhold `human:approved` on the first task until that PR
  merges, rather than writing `**Blocked by:** #<pr>`.

Once filed correctly, the cascade is automatic: T1 claims plain and
opens `claude/<T1>-*`; the scout enriches T2 with `stackable_blocker_pr`
pointing at T1's PR; the next worker claims T2 `--stackable-on <T1-PR>`
and branches off T1's head; and so on. The merger re-targets each
child's base onto master as upstreams merge.
