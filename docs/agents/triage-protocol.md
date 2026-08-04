# Triage protocol — issue triage against the standing objectives

The judgment layer between "an issue exists" and "the human approves it".
Triage reads every **untriaged** open issue (no `fleet:` / `human:`
labels), judges it against the standing direction in the target repo's
**objectives-path** and the fleet's scoping rules, and produces a verdict
the human can act on in one read.

The protocol has **two invocation modes**, sharing the same inputs and the
same per-issue judgment:

| Mode | Who runs it | Output | Approval |
|---|---|---|---|
| **Dispatched dry-run** (§ Dispatched dry-run mode) | the transient `triage` role, on the singleton host | a `## Triage` comment + the inert `fleet:triage-recommend` label | never — the human acts from the digest |
| **Architect-managed sweep** (§ Architect-managed sweep) | an architect session, on human cue | a staged report under `~/.fleet/triage/`, then labels applied only after the human confirms | the human's, executed by the session |

**Neither mode approves on its own judgment.** The dispatched role
recommends and stops; the sweep stages and waits for an explicit human
confirmation before any label is written. Graduating any verdict class to
autonomous approval (`fleet:agent-approved` stamping without a human in the
loop) is a separate, human-authorized protocol change — see § Graduation.

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

The `fleet:*` / `human:*` label vocabulary is **shared infrastructure**, not
a per-repo delta — every fleet-enabled repo uses the catalog in
[`fleet-labels-reference.md`](fleet-labels-reference.md). The genuinely
per-repo classification reference is **objectives-path**.

## Hard rules

- **Never close an issue, never edit an issue body or title.** Both modes.
  Closing is the human's act; a recommend-close verdict is staged or
  commented, never executed.
- **Never approve on the role's own judgment.** The dispatched mode adds
  nothing but `fleet:triage-recommend`; the sweep mode writes labels only
  from a staging file the human has explicitly confirmed, and never writes
  `fleet:queued` / `fleet:task` / verdict labels at all (those are
  scout- and ingest-owned — see [`TASK-FILING.md`](TASK-FILING.md)).
- **Read-only on the tree.** Cheap verification only (grep, file reads,
  `gh` queries) — no builds, no runs.
- **One target repo per run.** A run reads exactly that repo's issues and
  its **objectives-path**, and never cites another repo's private content
  in output that lands on a public repo
  ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) § Cross-repo information
  isolation). Staged sweep files live under `~/.fleet/`, outside every
  repo tree, so downstream issue content never reaches a public clone.

## Inputs

1. Every `Status: active` objective under **objectives-path** — the
   direction issues are judged against. A repo with no objectives docs
   yet degrades to defect-shaped / park classification, and the run says
   so explicitly rather than inventing direction.
2. The untriaged set: open issues with no `fleet:` / `human:` label (the
   same predicate `fleet-decisions` surfaces as the untriaged cue), minus
   the mode's idempotency skips.
3. The issue thread in full — body AND comments.
4. [`TASK-FILING.md`](TASK-FILING.md) (the structured-body bar) and
   [`fleet-labels-reference.md`](fleet-labels-reference.md).

Process oldest-first, capped at **10 issues per run** — bounded token
spend; the backlog drains across runs.

## Per-issue judgment

Verify before classifying: an issue's premise is a claim. Grep the cited
files/symbols; check whether a named defect is already fixed on `master`;
search for an open duplicate. Then classify into exactly one verdict:

- **recommend-approve** — serves an active objective (name the objective
  and the specific Done-means row) or is defect-shaped with credible,
  verifiable forensics; scoped to a plannable surface. Include a suggested
  `**Model:**` class and, if the body lacks it, the `**Objective:** <slug>`
  line the filer should add.
- **park (needs-human)** — direction-shaped (new capability, public-API
  surface, design choice), crosses an objective's Non-goals, targets gated
  self-config, or has a premise you could not verify either way.
- **recommend-close** — duplicate (cite the open issue) or already
  shipped (cite the merged PR / the master evidence you checked).
- **insufficient-info** — name exactly what is missing (repro command,
  observed output, the file the report is about). Vague "needs more
  detail" verdicts are banned; the comment must let the filer fix the gap
  in one edit.

## Dispatched dry-run mode

The transient `triage` role. Its outputs are exactly two things: a
`## Triage` comment on the issue and the inert `fleet:triage-recommend`
label. Never add `human:approved`, `fleet:agent-approved`, `fleet:queued`,
or any model-affinity label.

- **Singleton by designation.** Run only on the host opted in with the
  **singleton-env** flag (the `FLEET_EPIC_STEWARD` pattern). Exactly one
  host in the fleet carries the flag — triage spends LLM judgment and
  posts comments, which are not idempotent across hosts.
- **Idempotent by guard, regardless of designation.** Skip any issue that
  already carries `fleet:triage-recommend` OR already has a `## Triage`
  comment. A misconfigured second host must converge, not double-post.

### The `## Triage` comment

```markdown
## Triage

**Verdict:** recommend-approve | park | recommend-close | insufficient-info
**Objective:** <slug + Done-means row, or "(none — defect-shaped)" or "(none — see verdict)">
**Suggested model:** opus | sonnet | (n/a)
**Basis:** <2-5 lines: what you verified in the tree, what you searched,
what the issue serves or duplicates — citations, not vibes>
```

Post the comment and add `fleet:triage-recommend` in immediate succession.
The label routes the issue into `fleet-decisions`' decision list, so the
verdict reaches the human through the digest push — the human then acts
with ordinary mechanics (`human:approved`, close, `human:owned`, or a
reply asking for changes) and removes the label, which also re-arms the
idempotency guard if they want a re-triage after edits.

### Invocation

Cue-driven today: the human says "triage sweep", or a cron one-shot runs
on the designated host. The engine instantiation of the delta keys:

```
cd ~/src/IrredenEngine/.claude/worktrees/pool-0 && \
  FLEET_TRIAGE=1 fleet-dispatch-wrap pane-0 "$FLEET_MODEL_OPUS" high triage "" live
```

Fleet-native scout → dispatcher wiring (untriaged fetch, projection,
trigger, `FLEET_TRIAGE` dispatch gate) is tracked in #2494.

This mode stays **engine-only** until a downstream fleet opts in by adding
its own `role-triage.md` wrapper answering the delta keys above. The sweep
mode below needs no wrapper — an architect session supplies the deltas from
its own repo context.

## Architect-managed sweep

The staged-approval mode. Where the dispatched role posts a verdict and
waits for the digest round-trip, the sweep runs **inside the target repo's
architect session**, so the human-in-the-loop is already present: the
architect enumerates, judges, and stages; the human confirms in
conversation; only then are labels written. This is the mode that gives a
downstream repo triage coverage without standing up a dispatched role for
it.

**Cue-driven, never autonomous** — the human says "triage sweep
[repo]". Like the objectives sweep in
[`architect-protocol.md`](architect-protocol.md), a filing/labeling pass
without a cue violates the stand-by contract.

### 1. Enumerate

```
fleet-triage-sweep list --repo <repo-slug>
```

Prints the untriaged set oldest-first (the same predicate as § Inputs),
and annotates any issue that appears in a prior staging file under
`~/.fleet/triage/` with that verdict and date — so a re-surfaced issue
doesn't re-spend judgment that was already spent and rejected.

### 2. Judge

Apply § Per-issue judgment against the target repo's **objectives-path**:
the same four verdict classes, the same verify-before-classifying bar, the
same cheap-verification-only rule. No `## Triage` comment is posted in this
mode and `fleet:triage-recommend` is not used — the report to the human is
the output, and the untriaged predicate is the idempotency guard (an
applied label removes the issue from the set; a rejected entry legitimately
re-surfaces, annotated).

### 3. Stage

Write one staging file at `~/.fleet/triage/<repo>-sweep-<date>.json`, and
present the same content to the human in conversation:

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

Proposed label sets draw from the shared catalog only, and the tool
enforces a narrow allowlist: `human:approved`, `human:no-plan`,
`human:owned`, and one of `fleet:sonnet` / `fleet:opus` / `fleet:fable`.
`fleet:queued` and `fleet:task` are deliberately absent — the scout and
ingest own those.

### 4. Confirm

The human confirms or rejects per issue or in batch, in conversation. The
architect records the decision by setting the top-level `human_confirmed`
to `true` and each accepted entry's `confirmed` to `true`. Rejected entries
stay `false` and are simply not applied.

### 5. Apply

```
fleet-triage-sweep apply --repo <repo-slug> ~/.fleet/triage/<file>.json [--dry-run]
```

Refuses outright unless both confirmation markers are set. Before each
write it re-fetches the issue's labels and **skips any issue that gained a
`fleet:` / `human:` label since staging** — the race guard against a
parallel human or ingest action. Applied issues are appended to
`~/.fleet/triage/log.jsonl` as an audit trail.

**Closes stay human-executed.** A recommend-close entry is reported with a
ready-to-run `gh issue close` line; neither `list` nor `apply` ever closes
an issue, confirmed or not. That keeps the one irreversible action out of
agent hands entirely.

### 6. Extended sweep — backlog drains

Issue triage is one of three human-gated backlogs; the other two — the
`fleet:coding-improvement` ticket pile and the `~/.fleet/feedback/`
channel — have their own cue-only drain skills
(`triage-coding-improvements`, `review-fleet-feedback`) because their
verdicts and gated self-config edits need a human. Their gauges live in
`fleet-decisions`, whose cues flip to OVERDUE past the drain thresholds
(12 open coding-improvement tickets; a `.last-reviewed` marker 14+ days
old or absent).

After step 5, check `fleet-decisions` and continue into each drain whose
cue shows — `triage-coding-improvements` with the present human answering
the verdict round, then `review-fleet-feedback`. The human's sweep cue
carries the drain cues; both skills stay cue-only in every other context,
and the dispatched dry-run mode never runs them — its host reaches the
gauges through the `fleet-decisions` digest instead. Issues run first:
the per-issue pass is cheap and bounded, and the drains benefit from the
fresher picture.

## Graduation (not in effect)

Autonomous approval is earned, not assumed. The human audits verdict
quality over a sustained window (the digest surfaces every dispatched
verdict; the sweep's staged report surfaces every swept one — spot-check
`Basis` claims either way), then explicitly authorizes flipping **bounded
classes** — e.g. sonnet-scale, single-module, defect/parity/test-debt — to
stamp `fleet:agent-approved` directly, while direction-shaped work stays
recommend-only forever. That flip is a change to this protocol plus the
role doc (gated self-config, human-applied by definition). Until it lands,
no verdict reaches a label without a human: the dispatched mode's verdicts
are advisory, and the sweep's are applied only through § Architect-managed
sweep step 4's explicit confirmation.
