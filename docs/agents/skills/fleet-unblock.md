# fleet-unblock — shared flow

Find what is stuck across the fleet's repos, unblock what the session has
the authority to unblock, hand the human only what needs a human, and
file the next work the state calls for. The deterministic half is the
read-only `fleet-survey` tool; the acting half is this flow.

**Cue-only, never auto-run.** Cues: "unblock the fleet", "fleet unblock",
"why is the fleet idle", "is anything really blocked", "what should the
fleet work on next". An objectives sweep or a triage sweep may follow
from its findings; this flow does not replace either.

Each repo's `.claude/skills/fleet-unblock/SKILL.md` is a thin wrapper that
points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)).

---

## Repo deltas this flow needs

| Delta key | What it is |
|---|---|
| **repos** | The repo keys and slugs the tool reads (`engine` → `owner/name`, `game` → `owner/name`). |
| **objectives-path** | Where the standing objectives live; new-work proposals are judged against them. |
| **triage staging dir** | Where `fleet-triage-sweep` staging files go (`~/.fleet/triage/`). |
| **decisions tool** | The human-digest tool (`fleet-decisions`); the flow consumes its merge queue and decisions list, never repeats it. |
| **filing lane** | The follow-up lane for verified fleet defects (`TASK-FILING.md` § Agent-approved follow-up lane) and its fired-incident bar. |
| **granted classes** | Which rows of the authority table the repo's owner has granted to a cued run. A row not granted is staged for the human instead. |
| **feedback file** | The session's end-of-iteration feedback file. |

---

## Hard rules

- **Act only inside the authority table, and only on a cue.** Everything
  else is staged with its command for the human. Label writes for
  untriaged issues go through `fleet-triage-sweep apply` so the race guard
  and the audit log still run.
- **Every close and every approval cites its evidence** in the comment or
  the staging basis: the merged PR, the plan comment, the umbrella, the
  file read. A verdict without a citation is not executed.
- **Never claim queue work, never merge, never push to a worker's branch.**
  A stranded PR is handed to the resume lane or named in the report.
- **Fleet-infrastructure defects need a fired incident** (a log line, a
  declined dispatch, a park comment) before they file through the lane.

## Authority table

| Finding | The session does | The human does |
|---|---|---|
| Ghost: a queued issue delivered by a merged PR (`shadow_merged_pr` set, no PR in flight, a decline or the thread confirms nothing is left) | closes it: `gh issue close <N> --comment "Delivered by PR #<M>; <residual owner if any>"` | nothing |
| Unapproved `fleet:task` that is defect-shaped with verifiable forensics, an epic child under a `human:approved` umbrella, or filed by the human with a structured body | stamps `human:approved` plus the body's model label | nothing |
| Unapproved `fleet:task` that is direction-shaped: a new capability, a public API addition, a Non-goal crossing, a design choice | nothing; reports a one-line recommendation and the question | decides |
| Untriaged issue | judges per `triage-protocol.md`, writes the staging file, confirms the granted-class entries itself and runs `fleet-triage-sweep apply` | confirms the rest |
| `fleet:needs-human` whose open question is factual: does X hold on master, did Y merge, what does the measurement read | gathers the evidence, posts it, removes the label, takes the resulting action (close, approve, unpark) | nothing |
| `fleet:needs-human` whose question is a product or design call | posts the recommendation and the question in one line | decides |
| `fleet:awaiting-infra` park whose named blocker is a ghost | closes the ghost; reconcile R8 lifts the park | nothing |
| Park whose blocker PR merged but whose issue stays open for a check | the factual needs-human row above | nothing |
| Park with no parsable `Parked-until:` line | repairs the line when the park comment makes the blocker unambiguous, and says so | nothing |
| Stranded `fleet:wip` PR with no owner label | leaves it to reconcile R7; when it is a chain head and conflicts, posts the land order on it | nothing |
| `fleet:design-blocked` PR | `architect-protocol.md` § Handling `fleet:design-blocked` PRs | nothing |
| Orphan claim labels, a stack whose base merged | `architect-protocol.md` self-heal step | nothing |
| Epic whose only remaining children are ghosts or closed | closes the ghosts; the steward closes the umbrella | nothing |
| `fleet:approved` PR in the merge queue | nothing | merges |
| Human-filed issue that should close as not planned, fleet self-config, an objective amendment | nothing; reports it | decides |

## Step 1 — Survey

```
fleet-survey                 # both repos
fleet-survey --repo engine
fleet-survey --json          # for a report artifact
```

A stale cache means `fleet-up` is not running: say so and stop. Read the
sections in order (dispatch health, queue partition, parks, stranded WIP,
approval gap, epics, untriaged), then the **decisions tool** for the merge
queue and the human-parked items.

## Step 2 — Explain the idle fleet

One line per cause, from the queue partition:

| bucket | what it means | what unblocks it |
|---|---|---|
| `ghost` | a queued task delivered by a merged `Refs` PR; workers decline it | the ghost close (authority table) |
| `pinned_away` | `**Host:**` or `fleet:needs-gl-host` for another OS | a satellite host, or a wrong pin corrected in the body |
| `in_flight` | an open PR exists | that PR's own state |
| `blocked` | `**Blocked by:**` names an open issue | the named issue; check whether it is itself a ghost |
| `in_progress` | a pane holds the claim | nothing unless the owner's heartbeat is stale |
| `claimable_here` | real work | nothing; if the dispatcher is still idle, read its log |

Dispatch health is the second half: a stale cache, a closed usage gate, or
a decline loop (`idle ticks` high while `claimable_here` is non-empty).

## Step 3 — Unblock PRs

Work the parks, the stranded list, the design-blocked set and every PR
the **decisions tool** parks on a human label, each against its row in the
authority table. Order: ghost closes first (they lift parks and epics for
free), then factual needs-human items, then state repairs. Post what you
did on the PR in one comment; the labels move through the named
transitions (`fleet-transition`), never two separate edits.

## Step 4 — Unblock issues

Then the issue side: ghosts, the approval gap (epic children first), the
needs-human items, and the untriaged set through the triage sweep. Each
executed row gets its citation; each non-granted row gets its one-line
recommendation and command in the report.

## Step 5 — Create the next work

After the unblocks, the state says what is missing:

- A fleet defect with a fired incident → the **filing lane**, one issue
  per defect, after a duplicate search.
- Per `active` objective under **objectives-path**: the next plannable
  slice this host can run, filed unlabeled with `**Objective:** <slug>`
  (direction-shaped work waits for `human:approved`; a decomposition goes
  through `file-epic`).
- A residual carved off an in-flight ticket → planning, never queued as a
  hypothesis (`architect-protocol.md` § Filing tasks).
- An epic the unblocks made close-out ready → say so; the steward closes.

When no claimable work remains for this host after the unblocks, say so
and name the two proposals that would change that.

## Step 6 — Report and feedback

Three lists: **executed** (each action with the command that ran and its
citation), **for the human** (merges, product calls, non-granted rows,
each with its command), **filed** (issues with numbers). Then append the
session's snags to the **feedback file**.

## Anti-patterns

- Reporting the queue count as available work without the partition.
- Closing a ghost without citing the PR that delivered it, or stripping
  `fleet:queued` instead of closing.
- Approving direction-shaped work because it is well written.
- Treating `fleet:awaiting-infra` as always legitimate; the park's reason
  can lapse while the label stays (the stalled sweep exempts it).
- Re-listing the decisions tool's merge queue as a finding.
- Filing a fleet-infrastructure issue from a source read alone.
