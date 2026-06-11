# triage-coding-improvements — shared flow

The canonical `triage-coding-improvements` flow: the **consumption side** of
the coding-improvement channel. `assess-coding-improvement` files tickets one
at a time as workers fix PR feedback; this skill drains that backlog in
batches — sweep the open `fleet:coding-improvement` tickets, cluster them by
target surface, triage each with the human (accept / reject / defer /
escalate placement), apply the accepted rule changes, and bundle them into
**one PR per run** instead of one micro-PR per ticket.

It is **cue-only** — never auto-run. The tickets are deliberately left
un-queued because most targets are gated self-config (role docs, skills,
review checklists) that autonomous workers may not edit. This skill is how
the human spends that judgment: the human cues the run, decides each verdict,
and merges the resulting PR. Every change still ships through the normal
review pipeline — the skill is the human's hands, not a bypass.

Every repo that runs a fleet keeps its
`.claude/skills/triage-coding-improvements/SKILL.md` as a thin wrapper that
points here and supplies only its **deltas**. The *flow* is single-sourced
here so the wrappers can't drift on mechanics. See
[`docs/design/skill-sharing.md`](../../design/skill-sharing.md).

Wherever a step needs a repo-specific value it names a **delta key** in bold.
The convention-surface keys are deliberately the **same keys**
`assess-coding-improvement` uses — the two skills are the two ends of one
channel and must agree on where rules live.

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

- On explicit human cue only: "triage coding improvements", "absorb the
  coding-improvement backlog", "work through the coding-improvement tickets".
- Never proactively, never from an autonomous role loop. (A worker that
  notices a large backlog may *mention* it to the human; it must not run
  this.)

A natural cue rhythm is when the open-ticket count reaches a handful, or
before a stretch of fleet activity so the absorbed rules pay off immediately.

---

## Step 1 — Sweep

Pull every open ticket with its full body, plus the recently-closed set for
the closed-loop check in Step 2:

```
gh issue list --repo <repo> --label fleet:coding-improvement --state open \
  --json number,title,body,comments
gh issue list --repo <repo> --label fleet:coding-improvement --state closed \
  --limit 50 --json number,title,body,stateReason
```

Each open ticket should carry (per the `assess-coding-improvement` body
shape): a **Class** (A: missing rule / B: exists but didn't fire), a **target
artifact** path, a one-line **proposed change**, and an **Occurrences** list.
A ticket missing these is still triaged — reconstruct what you can from its
context and say so in the digest.

`Recurred:` comments on a ticket are occurrence evidence — count them.

## Step 2 — Cluster, cross-dedup, closed-loop check

The filing-side dedup only checks at file time, so overlap accretes between
tickets. Before triage:

1. **Cluster by target surface** — group tickets whose target artifact is the
   same file (or the same surface class: style baseline, module doc,
   automated check, review checklist, scripts/tooling docs).
2. **Cross-dedup** — two open tickets proposing the same rule for the same
   artifact merge into one digest entry (the verdict will close both).
3. **Closed-loop check** — for each open ticket, search the closed set for a
   ticket targeting the same artifact/rule:
   - Closed-as-**completed** match → the rule already landed and the mistake
     **recurred anyway**. The surface didn't fire. Recommend
     **escalate placement** (see Step 3), not re-adding the same text.
   - Closed-as-**not-planned** match → the human previously rejected this
     rule and it came back. Surface that history in the digest — recurrence
     of a rejected rule is evidence to reconsider, but the human decides.

## Step 3 — Triage with the human

Present one compact digest: per cluster, each ticket's number, class, target
artifact, the one-line proposed change, occurrence count, any closed-loop
history, and **your recommended verdict**. Then gather verdicts in one round
(a question per cluster when there are few; a free-form table response when
there are many). Verdicts:

- **ACCEPT** — apply the proposed change to the proposed artifact.
- **ESCALATE PLACEMENT** — accept the rule but move it up the enforcement
  ladder: a Class-B "doc rule that didn't fire" with a mechanically
  detectable pattern becomes a check on the **automated-check surface**
  instead of more doc text; a rule buried in a rarely-read doc relocates to
  the surface the author actually hits. This is the strongest lever and the
  one a lazy triage skips — recommend it whenever the ticket's pattern is
  grep-able or the closed-loop check fired.
- **RESCOPE** — accept a tighter version (different artifact, shorter rule,
  example folded into an existing bullet). State the rescoped one-liner in
  the digest so the human approves the actual text destiny, not a vibe.
- **REJECT** — close as not-planned, with a one-line reason comment. Typical
  reasons: too niche to spend surface budget on, already covered adequately,
  cost of the rule exceeds the mistake it prevents.
- **DEFER** — leave open untouched (e.g. blocked on an in-flight refactor of
  the target surface). Say why in the digest; a deferred ticket should name
  what unblocks it.

**Net-growth discipline** (apply when recommending): convention surfaces are
read by every worker on every task — their budget is the scarcest resource
this skill spends. Prefer tightening or exemplifying an **existing** bullet
over adding a new one. A new rule is one bullet at the surface's existing
altitude, not a paragraph. If a single surface would gain more than ~5 lines
in one batch, look for a consolidation before applying. Multi-occurrence
tickets earn their lines; single-occurrence Class-A tickets are the first
candidates for REJECT or DEFER.

## Step 4 — Apply the accepted changes

Work on a fresh feature branch (the **commit skill** handles branch
mechanics; never on the default branch). For each ACCEPT / ESCALATE /
RESCOPE:

- **Doc/rule edits**: make the edit exactly as triaged. Match the target
  surface's existing voice and altitude. Do not reword neighboring rules
  while you're there — this PR's diff should map 1:1 to triaged verdicts.
- **Automated-check changes** (the ESCALATE path): extend the check, then
  **validate it fires** against the ticket's cited Occurrence (check out or
  reconstruct the bad pattern from the cited `file:line`) and does not fire
  on the corrected version. An enforcement change that was never seen to
  fire is doc text with extra steps.
- **Headless note**: if this run is somehow in a headless session, `.claude/`
  paths need the repo's gated-edit tool (engine: `fleet-edit`) instead of
  the `Edit` tool. In the normal human-cued interactive session, plain edits
  are fine.

## Step 5 — Route the outliers

Some tickets imply real code or tooling work beyond the rule text (a "live
deviation to migrate", a flag to canonize, a script to change). Do **not**
stuff code changes into the convention-surface bundle. Instead:

- Land the rule text in the bundle as triaged.
- File the code work as a separate issue per the repo's **filing norms**
  (typically: plain issue, no labels, the human approves it into the queue),
  cross-referencing the coding-improvement ticket.
- The bundle PR body lists these split-outs so the human sees the routing.

A ticket whose rule landed and whose remaining work has its own issue is
**done** — close it via the bundle PR, with the split-out issue carrying the
remainder.

## Step 6 — Bundle and ship

One PR per triage run, via the **commit skill**:

- Branch/commit scope per the **scope vocabulary** (e.g.
  `docs/fleet: absorb coding-improvement batch — <theme>`).
- PR body: the triage digest (verdict per ticket, including REJECTs and
  DEFERs so the run is auditable), `Closes #N` for every ticket fully
  addressed by this PR, and the split-out issue links from Step 5.
- Split a **second** PR only when an automated-check change wants isolated
  review/verification — doc-text and check-logic changes have different
  review costs. Otherwise resist per-surface PR splitting; the whole point
  is one reviewable batch.
- REJECTed tickets were already closed at triage time (Step 3) with their
  reason comment — they are not `Closes` targets.

The PR goes through the normal review pipeline. Gated self-config files in
the diff are expected here — the reviewer checks the changes match the
triage digest in the PR body.

## Step 7 — Report

One short block back to the human:

- Tickets swept / clustered / merged-as-duplicates.
- Verdict tally: accepted, escalated, rescoped, rejected, deferred.
- Surfaces touched and net line growth per surface (the budget number).
- Closed-loop findings: any rule that landed before and recurred (and what
  was escalated because of it).
- The PR link, split-out issue links, and what remains open (DEFERs).

---

## Anti-patterns

- **Accepting everything.** The channel's filing side is calibrated to
  over-propose slightly (one occurrence is enough to file). Triage exists to
  say no. A run that rejects nothing probably wasn't triage.
- **Queueing gated self-config to workers.** If a verdict needs `.claude/`
  or role-doc edits, this run makes them; don't convert the ticket into a
  `fleet:queued` task an autonomous worker would be blocked on.
- **One PR per ticket.** Eleven one-line doc PRs is overhead and
  merge-conflict bait; the bundle is the design.
- **Silent rewording.** Applying a different rule than the digest showed —
  every deviation from the ticket's proposed text goes through RESCOPE.
- **Enforcement changes never seen to fire.** Validate automated-check edits
  against the cited occurrence (Step 4) or downgrade the verdict to a doc
  rule.
- **Letting the run mutate engine/creation code.** Code work routes out via
  Step 5; this PR touches convention surfaces only.
