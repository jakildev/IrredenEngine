# triage-coding-improvements — shared flow

The consumption side of the coding-improvement channel. Sweep the open
`fleet:coding-improvement` tickets, cluster them by target surface, triage
each with the human, apply the accepted rule changes, and ship them as
**one PR per run**.

**Cue-only — never auto-run.** The tickets stay un-queued because most
targets are gated self-config (role docs, skills, review checklists); this
skill is how the human spends that judgment. The PR still goes through the
normal review pipeline.

Each repo's `.claude/skills/triage-coding-improvements/SKILL.md` is a thin
wrapper that points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)). The
convention-surface keys are the same keys `assess-coding-improvement`
uses — the two ends of one channel must agree on where rules live.

---

## Repo deltas this flow needs

| Delta key | What it is |
|---|---|
| **repo** | The `gh --repo` slug for `gh issue` calls. |
| **convention surfaces** | The ordered set of docs/rules/checks that encode this repo's conventions — the legal targets for an accepted rule. Same key as `assess-coding-improvement`. |
| **automated-check surface** | Where a mechanically-detectable rule is *enforced* pre-commit. Same key as `assess-coding-improvement`. |
| **review checklist** | The repo's review criteria — backstop enforcement surface. Same key as `assess-coding-improvement`. |
| **commit skill** | The repo's commit/PR skill (e.g. `commit-and-push`) used to ship the bundle. |
| **filing norms** | How to file a split-out code task so it enters the repo's queue correctly (label rules, who approves). |
| **scope vocabulary** | The commit/PR scope prefix for a convention-surface batch (e.g. `docs/fleet:`). |

---

## When to run

On explicit human cue only ("triage coding improvements", "absorb the
coding-improvement backlog", "work through the coding-improvement
tickets"); an architect-run interactive triage sweep
(`docs/agents/triage-protocol.md` §"Extended sweep") counts. Cue at 10–15
open tickets — `fleet-decisions` flips the cue to OVERDUE at 12; larger
batches turn the verdict round into a wholesale approve.

## Step 1 — Sweep

```
gh issue list --repo <repo> --label fleet:coding-improvement --state open \
  --json number,title,body,comments
gh issue list --repo <repo> --label fleet:coding-improvement --state closed \
  --limit 50 --json number,title,body,stateReason
```

`comments` returns full bodies, so `Recurred:` entries are already present
— count them as occurrences. The **observations-ledger** issue
(`assess-coding-improvement.md` Step 4) carries `human:owned`, not this
label, so it is outside the sweep; ungraduated ledger entries on a
cluster's target artifact may count as extra occurrence evidence.

Each ticket should carry a Class (A/B), a target artifact, a one-line
proposed change, and an Occurrences list; reconstruct what a malformed one
lacks and say so in the digest.

## Step 2 — Cluster, cross-dedup, closed-loop

1. Cluster by target artifact (or surface class: style baseline, module
   doc, automated check, review checklist, tooling docs).
2. Merge open tickets proposing the same rule for the same artifact into
   one digest entry.
3. For each open ticket, search the closed set for the same artifact/rule:
   closed-as-**completed** → the rule landed and recurred anyway; recommend
   **escalate placement**, not the same text again. Closed-as-**not-planned**
   → previously rejected; surface the history, the human decides.

## Step 3 — Triage with the human

One digest: per cluster, each ticket's number, class, target, proposed
change, occurrence count, closed-loop history, and your recommended
verdict. Gather verdicts in one round.

- **ACCEPT** — only when the change is a validator: a check, ratchet,
  lint, or test the fleet executes (the automated-check surface or CI).
  The check's failure message carries the rule; no prose is added.
- **ACCEPT AS FACT** — a prose line only for something no validator can
  express and the model cannot derive (a build command, an invariant, a
  platform gotcha), at its canonical home, within that file's
  instruction-size budget, replacing text rather than adding to it.
- **REJECT** — everything else, closed with a one-line reason: the default
  for process reminders, verification instructions, style preferences,
  and anything the model does unprompted.
- **DEFER** — leave open; name what unblocks it.

FLEET.md §"Improvement posture" is the bar. Single-occurrence tickets and
anything without a fired incident are the first REJECT candidates.

## Step 4 — Apply

On a fresh feature branch (the **commit skill** owns branch mechanics).
Doc edits map 1:1 to the triaged verdicts — no rewording of neighbours.
An automated-check change must be seen to fire on the ticket's cited
occurrence (reconstruct the bad pattern from the cited `file:line`) and
stay quiet on the corrected version; otherwise downgrade to a doc rule.
In a headless session, `.claude/` paths need the repo's gated-edit tool
(engine: `fleet-edit`).

## Step 5 — Route the outliers

Code or tooling work implied by a ticket does not go in the bundle. Land
the rule text, file the code work as its own issue per the **filing
norms** (cross-referencing the ticket), and list the split-outs in the PR
body. A ticket whose rule landed and whose remainder has its own issue is
done.

## Step 6 — Bundle and ship

One PR per run via the **commit skill**: scope per the **scope
vocabulary**; body = the triage digest (every verdict, REJECTs and DEFERs
included), `Closes #N` per fully-addressed ticket, split-out links.
REJECTed tickets were closed at triage time and are not `Closes` targets.
Split a second PR only when an automated-check change wants isolated
review. Gated self-config in the diff is expected; the reviewer checks the
changes match the digest.

## Step 7 — Report

Tickets swept / clustered / merged; verdict tally; surfaces touched with
net line growth each; closed-loop findings; PR link, split-out links,
remaining DEFERs.

---

## Anti-patterns

- Accepting everything — the filing side over-proposes by design; a run
  that rejects nothing was not triage.
- Converting a gated-self-config verdict into a `fleet:queued` task an
  autonomous worker cannot edit.
- Letting the run mutate engine/creation code (Step 5 routes it out).
