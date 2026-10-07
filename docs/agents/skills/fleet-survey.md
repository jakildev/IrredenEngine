# fleet-survey — shared flow

Answer "why is the fleet idle, what is really blocked, and what should it
do next" in one human-cued pass. The deterministic half is the
`fleet-survey` tool; the judgment half is this flow: verify what the tool
flags, stage the labels the human will confirm, file the fleet defects you
verified, and hand the human one checklist.

**Cue-only, never auto-run.** Cues: "fleet survey", "survey the fleet",
"why is the fleet idle", "is anything really blocked", "what should the
fleet work on next". An objectives sweep or a triage sweep may follow from
its findings; this flow does not replace either.

Each repo's `.claude/skills/fleet-survey/SKILL.md` is a thin wrapper that
points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)).

---

## Repo deltas this flow needs

| Delta key | What it is |
|---|---|
| **repos** | The repo keys and slugs the tool reads (`engine` → `owner/name`, `game` → `owner/name`). |
| **objectives-path** | Where the standing objectives live; next-chunk proposals are judged against them. |
| **triage staging dir** | Where `fleet-triage-sweep` staging files go (`~/.fleet/triage/`). |
| **decisions tool** | The human-digest tool (`fleet-decisions`); the survey never duplicates its merge queue or decisions list. |
| **filing lane** | The follow-up lane for verified fleet defects (`TASK-FILING.md` § Agent-approved follow-up lane) and its fired-incident bar. |
| **feedback file** | The session's end-of-iteration feedback file. |

---

## Hard rules

- **Read-only on labels until the human confirms.** The tool writes
  nothing. The session stages triage verdicts and lists label and close
  commands; it applies a label only through `fleet-triage-sweep apply` on
  a human-confirmed file, and never closes an issue.
- **Never claim queue work** and never push to a worker's branch. A
  stranded PR is handed to the resume lane or to the human, not adopted.
- **Every "blocked" verdict names what unblocks it.** A finding without an
  unblock line is not a finding.
- **Fleet-infrastructure defects need a fired incident** (a log line, a
  declined dispatch, a park comment) before they file through the lane.

## Step 1 — Run the tool

```
fleet-survey                 # both repos
fleet-survey --repo engine
fleet-survey --json          # for a report artifact
```

It needs a fresh scout cache (`fleet-up` running). Read the sections in
order: dispatch health, the per-repo queue partition, parks, stranded WIP,
the approval gap, epics, untriaged. Then `fleet-decisions` for the merge
queue and the human-parked decisions; the survey assumes that list, it
does not repeat it.

## Step 2 — Explain the idle fleet

The queue partition says why panes sit empty. Write one line per cause:

| bucket | what it means | what unblocks it |
|---|---|---|
| `ghost` | a queued task delivered by a merged `Refs` PR; workers decline it | the human closes it (`gh issue close <N> --comment "delivered by PR #M"`), or a plan correction re-scopes the residual |
| `pinned_away` | `**Host:**` or `fleet:needs-gl-host` for another OS | a satellite host comes up (`fleet-up --satellite`), or the pin is wrong and the issue body is corrected |
| `in_flight` | an open PR exists; the issue is claimed by that PR | the PR's own state (review, park, feedback) |
| `blocked` | `**Blocked by:**` names an open issue | the named issue closes; check whether it is itself a ghost |
| `in_progress` | a pane holds the claim | nothing, unless the owner's heartbeat is stale |
| `claimable_here` | real work | nothing; if this is non-empty and the dispatcher is idle, read the dispatcher log |

Dispatch health adds the second explanation: a stale cache, a closed usage
gate, or a decline loop (`idle ticks` high with `claimable_here` non-empty).

## Step 3 — Audit the parks and the stranded WIP

For each `fleet:awaiting-infra` PR the tool resolves the `Parked-until:`
issues and gives a verdict. Act on each:

- **lifted** — the park cleared; reconcile R8 removes the label on its
  next tick, then R7 re-arms the resume lane. Nothing to do unless it has
  sat for hours, then note it for the human.
- **parked on a delivered issue** — the named issue is a ghost. The close
  in Step 2 lifts the park. Say so in the checklist.
- **waits on a human decision** — the named issue is `fleet:needs-human`.
  Read that issue's thread and, if the evidence to decide exists, post it
  there (the architect's comment is the reading; the human decides).
- **legit** — nothing to do.
- **malformed** — no parsable line; the PR accrues `fleet:state-drift`.
  Repair the line only when the intended blocker is unambiguous from the
  park comment, and say you did.

Stranded `fleet:wip` PRs (no claim, no park, no design label) resume
through reconcile R7 after the drift-tick threshold. A chain head that is
stranded and `CONFLICTING` is the one to watch: the whole chain waits on
its rebase. Name it in the checklist with its land order.

## Step 4 — Close the approval gap

`fleet:task` issues with no `human:approved` or `fleet:agent-approved` are
filed work nobody has ruled on. Epic children come first: a child under a
signed umbrella that never got its label is an oversight, not a decision.
For each row give the human a one-line recommendation (approve with model,
park, close as superseded) and the command.

## Step 5 — Stage triage

For the untriaged set run the triage sweep's judgment
([`triage-protocol.md`](../triage-protocol.md) § Architect-managed sweep):
verify each premise in the tree, write the staging file under the
**triage staging dir**, and leave `human_confirmed: false`. The human
confirms; then `fleet-triage-sweep apply`.

## Step 6 — File what you verified

A finding that is a fleet defect with a fired incident (a decline loop, a
refused claim in the dispatcher log, a park that can never lift) files
through the **filing lane**: a structured body with the log lines, the
reproduction you ran, and acceptance criteria naming the test file. A
defect you only reasoned about files unlabeled. One issue per defect;
search for an open duplicate first.

## Step 7 — Propose the next chunks

Per `active` objective under **objectives-path**, after the unblocks
above: which Done-means row has a plannable slice that this host can run,
and which epic becomes close-out ready. Keep it to what the human must
decide; direction-shaped proposals are filed unlabeled and wait for
`human:approved` (architect-protocol § Objectives sweep).

## Step 8 — The checklist

End with one list the human can execute top to bottom, grouped as
**merge**, **close**, **approve**, **decide**, **confirm staging**. Every
item carries its command. Then append the session's snags to the
**feedback file**.

## Anti-patterns

- Reporting the queue count as "available work" without the partition.
- Closing a ghost yourself, or stripping `fleet:queued` from it.
- Treating `fleet:awaiting-infra` as always legitimate; the park's reason
  can lapse while the label stays (the stalled sweep exempts it).
- Re-listing `fleet-decisions`' merge queue as a survey finding.
- Filing a fleet-infrastructure issue from a source read alone.
