# Triage protocol — dry-run issue triage against the standing objectives

The judgment layer between "an issue exists" and "the human approves it".
The triage role reads every **untriaged** open issue (no `fleet:` / `human:`
labels), judges it against the standing direction in
[`docs/design/objectives/`](../design/objectives/README.md) and the fleet's
scoping rules, and posts a verdict the human can act on in one read.

**This protocol defines DRY-RUN only.** The role recommends; it never
approves. Graduating any verdict class to live approval
(`fleet:agent-approved` stamping) is a separate, human-authorized protocol
change — see § Graduation.

## Hard rules

- **Outputs are exactly two things**: a `## Triage` comment on the issue and
  the inert `fleet:triage-recommend` label. Never add `human:approved`,
  `fleet:agent-approved`, `fleet:queued`, or any model-affinity label; never
  close an issue; never edit an issue body or title.
- **Singleton by designation.** Run only on the host opted in with
  `FLEET_TRIAGE=1` (the `FLEET_EPIC_STEWARD` pattern). Exactly one host in
  the fleet carries the flag — triage spends LLM judgment and posts
  comments, which are not idempotent across hosts.
- **Idempotent by guard, regardless of designation.** Skip any issue that
  already carries `fleet:triage-recommend` OR already has a `## Triage`
  comment. A misconfigured second host must converge, not double-post.
- **Read-only on the tree.** Cheap verification only (grep, file reads,
  `gh` queries) — no builds, no runs.
- **Engine repo only** until the human extends scope. Never read or cite
  the private game repo
  ([`CLAUDE-BASELINE.md`](CLAUDE-BASELINE.md) § Cross-repo information
  isolation).

## Inputs

1. Every `Status: active` objective in `docs/design/objectives/*.md` —
   the direction issues are judged against.
2. The untriaged set: open issues with no `fleet:` / `human:` label (the
   same predicate `fleet-decisions` surfaces as the untriaged cue), minus
   the idempotency-guard skips.
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

## The `## Triage` comment

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

## Invocation

Cue-driven today: the human says "triage sweep", or a cron one-shot runs
on the designated host:

```
cd ~/src/IrredenEngine/.claude/worktrees/pool-0 && \
  FLEET_TRIAGE=1 fleet-dispatch-wrap pane-0 "$FLEET_MODEL_OPUS" high triage "" live
```

Fleet-native scout → dispatcher wiring (untriaged fetch, projection,
trigger, `FLEET_TRIAGE` dispatch gate) is tracked in #2494.

## Graduation (not in effect)

Live approval is earned, not assumed. The human audits verdict quality
over a sustained window (the digest surfaces every verdict; spot-check
`Basis` claims), then explicitly authorizes flipping **bounded classes**
— e.g. sonnet-scale, single-module, defect/parity/test-debt — to stamp
`fleet:agent-approved` directly, while direction-shaped work stays
recommend-only forever. That flip is a change to this protocol plus the
role doc (gated self-config, human-applied by definition). Until it
lands, every verdict is advisory.
