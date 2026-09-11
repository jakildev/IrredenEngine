# Triage protocol — issue triage against the standing objectives

The judgment layer between "an issue exists" and "the human approves it".
Triage reads every untriaged open issue (no `fleet:` / `human:` label),
judges it against the target repo's **objectives-path** and the filing
rules, and produces a verdict the human acts on in one read. Two modes
share the same inputs and judgment:

| Mode | Who | Output | Approval |
|---|---|---|---|
| **Dispatched dry-run** | the transient `triage` role, on the singleton host | a `## Triage` comment + the inert `fleet:triage-recommend` label | never — the human acts from the digest |
| **Architect-managed sweep** | an architect session, on human cue | a staged report under `~/.fleet/triage/`, labels applied after the human confirms | the human's, executed by the session |

Neither mode approves on its own judgment; graduating a verdict class to
autonomous `fleet:agent-approved` stamping is a separate, human-authorized
protocol change (§ Graduation).

## Repo deltas this flow needs

| Delta key | Meaning |
|---|---|
| **repo-slug** | The target GitHub repo (`owner/name`) for `gh issue` calls. |
| **repo-root** | Absolute path of that repo's clone. |
| **worktree-path** | The pool worktree the role was dispatched into. Its basename (`basename $PWD`) is the agent name for heartbeats — never derive it from the role name. |
| **role-name** | The role's name for banners and feedback (e.g. `triage`). |
| **role-banner** | The one-line banner printed at startup. |
| **objectives-path** | Where the repo's standing objectives live — the direction issues are judged against. |
| **singleton-env** | The env flag designating the one host that runs the dispatched mode (e.g. `FLEET_TRIAGE=1`). |
| **feedback-file** | This role's end-of-iteration feedback file under `~/.fleet/feedback/`. |

The `fleet:*` / `human:*` vocabulary is shared infrastructure
([`fleet-labels-reference.md`](fleet-labels-reference.md)); the per-repo
classification reference is **objectives-path**.

## Hard rules

- **Never close an issue, never edit a body or title.** A recommend-close
  verdict is staged or commented, never executed.
- **Never approve on the role's own judgment.** Dispatched mode adds only
  `fleet:triage-recommend`; sweep mode writes labels only from a staging
  file the human confirmed, and never `fleet:queued` / `fleet:task` /
  verdict labels (scout- and ingest-owned — [`TASK-FILING.md`](TASK-FILING.md)).
- **Read-only on the tree** — grep, file reads, `gh` queries; no builds, no
  runs.
- **One target repo per run**, never citing another repo's private content
  in output that lands on a public repo ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md)
  § Cross-repo information isolation). Staging files live under
  `~/.fleet/`, outside every tree.

## Inputs

1. Every `Status: active` objective under **objectives-path**. A repo with
   no objectives degrades to defect-shaped / park classification and says so.
2. The untriaged set (the predicate `fleet-decisions` surfaces as the
   untriaged cue), minus the mode's idempotency skips.
3. The issue thread in full — body and comments.
4. [`TASK-FILING.md`](TASK-FILING.md) (the structured-body bar) and
   [`fleet-labels-reference.md`](fleet-labels-reference.md).

Oldest first, at most **10 issues per run**.

## Per-issue judgment

An issue's premise is a claim: grep the cited files and symbols, check
whether a named defect is already fixed on `master`, search for an open
duplicate. Then exactly one verdict:

- **recommend-approve** — serves an active objective (name it and the
  Done-means row) or is defect-shaped with verifiable forensics, scoped to a
  plannable surface. Include a suggested `**Model:**` and, if the body lacks
  it, the `**Objective:** <slug>` line to add.
- **park (needs-human)** — direction-shaped (new capability, public API,
  design choice), crosses a Non-goal, targets gated self-config, or has a
  premise you could not verify either way.
- **recommend-close** — duplicate (cite the open issue) or already shipped
  (cite the merged PR or the master evidence).
- **insufficient-info** — name exactly what is missing (repro command,
  observed output, the file); the comment must let the filer fix it in one
  edit.

## Dispatched dry-run mode

Outputs exactly two things: a `## Triage` comment and
`fleet:triage-recommend`. Never `human:approved`, `fleet:agent-approved`,
`fleet:queued`, or a model label.

- **Singleton by designation:** runs only on the host carrying the
  **singleton-env** flag (the `FLEET_EPIC_STEWARD` pattern).
- **Idempotent by guard:** skip any issue that already carries
  `fleet:triage-recommend` or has a `## Triage` comment.

```markdown
## Triage

**Verdict:** recommend-approve | park | recommend-close | insufficient-info
**Objective:** <slug + Done-means row, or "(none — defect-shaped)" or "(none — see verdict)">
**Suggested model:** opus | sonnet | (n/a)
**Basis:** <2-5 lines: what you verified in the tree, what you searched,
what the issue serves or duplicates — citations, not vibes>
```

Post the comment and add the label in immediate succession. The label routes
the issue into the `fleet-decisions` digest; the human acts with ordinary
mechanics (`human:approved`, close, `human:owned`, a reply) and removes the
label, which re-arms the guard for a re-triage.

Invocation (cue "triage sweep", or a cron one-shot on the designated host):

```
cd ~/src/IrredenEngine/.claude/worktrees/pool-0 && \
  FLEET_TRIAGE=1 fleet-dispatch-wrap pane-0 "$FLEET_MODEL_OPUS" high triage "" live
```

Engine-only until a downstream fleet adds its own `role-triage.md` wrapper;
the sweep mode needs no wrapper.

## Architect-managed sweep

Runs inside the target repo's architect session, so the human is present:
enumerate, judge, stage; the human confirms; then labels are written.
**Cue-driven only** ("triage sweep [repo]").

### 1. Enumerate

```
fleet-triage-sweep list --repo <repo-slug>
```

Prints the untriaged set oldest-first, annotating any issue that appears in
a prior staging file under `~/.fleet/triage/` with that verdict and date.

### 2. Judge

§ Per-issue judgment against **objectives-path**. No `## Triage` comment,
no `fleet:triage-recommend`; the report is the output and the untriaged
predicate is the idempotency guard (an applied label removes the issue from
the set; a rejected entry re-surfaces, annotated).

### 3. Stage

Write `~/.fleet/triage/<repo>-sweep-<date>.json` and present it in
conversation:

```json
{
  "repo": "owner/name",
  "generated_at": "<ISO-8601>",
  "human_confirmed": false,
  "entries": [
    {
      "number": 123,
      "title": "…",
      "verdict": "recommend-approve",
      "labels": ["human:approved", "fleet:sonnet"],
      "basis": "2-5 lines of citations, not vibes",
      "confirmed": false
    }
  ]
}
```

Proposed labels draw from a narrow allowlist the tool enforces:
`human:approved`, `human:no-plan`, `human:owned`, and one of `fleet:sonnet`
/ `fleet:opus` / `fleet:fable`. `fleet:queued` and `fleet:task` are absent
by design.

### 4. Confirm

The human confirms or rejects per issue or in batch. Set top-level
`human_confirmed` to `true` and each accepted entry's `confirmed` to `true`;
rejected entries stay `false` and are not applied.

### 5. Apply

```
fleet-triage-sweep apply --repo <repo-slug> ~/.fleet/triage/<file>.json [--dry-run]
```

Refuses unless both markers are set. Before each write it re-fetches labels
and skips any issue that gained a `fleet:` / `human:` label since staging.
Applied issues append to `~/.fleet/triage/log.jsonl`. **Closes stay
human-executed**: a recommend-close entry is reported with a ready-to-run
`gh issue close` line; neither `list` nor `apply` closes anything.

### 6. Extended sweep — backlog drains

Two sibling human-gated backlogs — the `fleet:coding-improvement` pile and
the `~/.fleet/feedback/` channel — have cue-only drain skills
(`triage-coding-improvements`, `review-fleet-feedback`); `fleet-decisions`
flips their cues to OVERDUE past the thresholds (12 open tickets; a
`.last-reviewed` marker 14+ days old or absent). After step 5, check
`fleet-decisions` and run each drain whose cue shows, issues first. The
sweep cue carries the drain cues; the dispatched mode never runs them.

## Graduation (not in effect)

Autonomous approval is earned: the human audits verdict quality over a
sustained window, then explicitly authorizes bounded classes (sonnet-scale,
single-module, defect / parity / test-debt) to stamp `fleet:agent-approved`
directly; direction-shaped work stays recommend-only. That flip is a change
to this protocol plus the role doc (gated self-config). Until then no
verdict reaches a label without a human.
