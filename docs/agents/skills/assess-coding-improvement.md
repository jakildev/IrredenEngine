# assess-coding-improvement — shared flow

The canonical `assess-coding-improvement` flow: after a worker has fixed the
feedback on a PR, confirm every comment was covered, then decide whether the
fix reveals a **generalizable** improvement to the fleet's development
procedures (style guide, coding rules, the `simplify` checks, the review
checklist, worker direction). If so, file — or append to — a tracking issue
tagged `fleet:coding-improvement` so the same class of mistake gets caught at
authoring time instead of being re-flagged on the next PR.

This is invoked at the **end of a feedback AMEND** (see
[`FLEET-FEEDBACK-HANDLING.md`](../FLEET-FEEDBACK-HANDLING.md) Step i). It is a
**reflection** pass, not a code change — the fix already landed. It never
touches the PR's code, labels, or claim.

This skill is the **filing side** of the channel; the backlog it produces is
drained in human-cued batches by
[`triage-coding-improvements.md`](triage-coding-improvements.md), the
consumption side.

Every repo that runs a fleet keeps its
`.claude/skills/assess-coding-improvement/SKILL.md` as a thin wrapper that
points here and supplies only its **deltas** — most importantly its
**convention surfaces** (the docs/rules/checks that encode that repo's
conventions). The *flow* is single-sourced here so the wrappers can't drift on
mechanics. See [`docs/design/skill-sharing.md`](../../design/skill-sharing.md).

Wherever a step needs a repo-specific value it names a **delta key** in bold.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug for `gh issue list` / `create` / `comment`. | `jakildev/IrredenEngine` |
| **comments tool** | The wrapper that surfaces all PR feedback in one call. | `fleet-pr comments <N>` |
| **convention surfaces** | The ordered set of docs/rules/checks that encode this repo's conventions — searched in Step 3 to decide whether a rule already exists. | the engine surfaces listed in the wrapper |
| **automated-check surface** | Where a mechanically-detectable rule is *enforced* pre-commit (so the worker gets it right the first time). | the `simplify` skill + its `simplify-*` subagents |
| **review checklist** | The repo's review criteria — a fallback enforcement surface when authoring keeps missing what review catches. | the `review-pr` wrapper's checklist |
| **observations-ledger** | The repo's append-only single-occurrence ledger: one parked issue (`human:owned` only — never `fleet:coding-improvement`, so the triage drain and the untriaged-inbox query never see it) where below-bar first occurrences are recorded as comments (Step 4). A repo that supplies no ledger runs the gate report-only. | issue #2903 |

The fleet label `fleet:coding-improvement` and the `gh issue` mechanics are
shared fleet machinery and are used concretely below.

---

## When to run

- Automatically, as the last step of a feedback AMEND on **any** path that
  changed code: `human:needs-fix` / `human:blocker`, `fleet:needs-fix`,
  `fleet:has-nits`. (Reviewer nits are the richest source — they are usually
  an instance of a convention that already exists somewhere.)
- On the ESCALATE path, only if the deferred concern is itself a convention
  (rare); the PR's code didn't change, so there is usually nothing to reflect
  on.
- On explicit ask ("should this be a fleet rule?", "assess coding
  improvement", "file a coding-improvement"). Explicit-ask filings use the
  same Step-4 body shape (Class / target artifact / proposed change /
  occurrences) — the shape is what makes batch triage mechanical, and a
  human-cued ticket that skips it forces the triage pass to reconstruct
  those fields from context.

Skip entirely if the only feedback was subjective preference or a pure
one-off domain fix (Step 2 gates this).

---

## Step 1 — Enumerate the feedback and confirm coverage

Re-run the **comments tool** for the PR:

```
fleet-pr comments <N>
```

(Add the repo flag for game PRs, per the wrapper.) Build an explicit
checklist with **one item per output line** — every `[comment …]`,
`[review … ]` summary, and `[path:line]` inline comment is a separate item.
The human (or reviewer) may have posted several comments and several inline
threads before tagging; none may be dropped.

For each item, confirm the fix you just pushed actually addresses it (or that
you explicitly accounted for it in the Step-e summary comment / ESCALATE
issue). If any item is uncovered, **stop the reflection and go back and
address it** — coverage comes before generalization.

This is the comprehensive-reading guarantee, re-asserted after the fix: the
same checklist that drives the fix drives the reflection.

## Step 2 — Is each item generalizable?

For every checklist item, decide: is this an instance of a **repeatable
pattern** — a convention, an idiom, or a footgun that recurs across code — or
a **one-off** specific to this code?

Generalizable (worth reflecting on), e.g.:
- a naming-convention slip,
- a math primitive that should have gone through the engine math layer,
- a per-entity component lookup in a tick / an allocation in a hot loop,
- an ECS ordering or ownership footgun,
- a doc-comment or test-coverage standard,
- a render-pipeline invariant.

One-off (skip — **no ticket**): a wrong magic number in one shader, an
off-by-one in one entity's spawn math, a typo, a subjective wording tweak.
**One-offs are the anti-spam gate** — most fixes are one-offs and produce
nothing here.

Carry forward only the generalizable items.

## Step 3 — Does the convention already exist? Classify the improvement

This is the heart of the pass. For each generalizable item, search the
**convention surfaces** to find whether the rule is already written down
somewhere. Then classify:

- **(A) Missing** — no surface encodes this rule. The improvement is to
  **add** it to the authoritative surface for its class (a naming rule to the
  style guide / naming rule file; an ECS rule to the ECS rule file; a render
  invariant to the render-review surface; etc.).

- **(B) Exists but didn't fire** — a surface already states the rule, yet the
  worker still shipped the mistake and a reviewer/human had to catch it. The
  real defect is **surfacing / enforcement**, not the rule text. Propose
  moving the rule to where the worker reliably hits it *at authoring time*:
  - if it's **mechanically detectable**, add or extend a check on the
    **automated-check surface** so the pre-commit pass flags it
    automatically — this is the strongest "get it right the first time" lever;
  - if it's buried in a rarely-read doc, **relocate / cross-link** it into the
    surface the worker actually reads at the relevant moment (e.g. the nearest
    module `CLAUDE.md`, or the author pipeline);
  - if review keeps catching what authoring misses, add it to the **review
    checklist** as a backstop (in addition to one of the above, not instead).

The ticket must state which class (A or B) and name the **exact target
artifact** (file path) plus the **one-line rule** to add or relocate.

## Step 4 — Dedup, then comment-or-file

Search open coding-improvement tickets first, so repeat occurrences accrue
evidence on one issue instead of spawning duplicates:

```
gh issue list --repo <repo> --label fleet:coding-improvement --state open \
  --json number,title,body
```

- **Match** (an open ticket targets the same artifact / rule) → add an
  occurrence instead of filing a new one:
  ```
  gh issue comment <M> --repo <repo> --body "Recurred: PR #<N>, <file:line> — \
  <one-line>. — <role-name>"
  ```
  A second occurrence is the signal this is a real pattern worth the human
  prioritizing.

Before filing on a **first (single) occurrence**, apply the
**second-occurrence-or-measured-misfire gate**. (This replaces the earlier
three-arm gate, whose "mechanically detectable" and "tightening an existing
surface" arms passed nearly every fleet-tooling generalization — 68 filings in
one week against a cue-only drain.) A standalone ticket files on a first
occurrence **only** for a **measured misfire**: a near-miss correctness/safety
defect that fired in something you ran or observed — a wrong result, a
false-clean pass, a real defect that would have shipped — not a failure mode
predicted from reading source. Everything else needs a **second occurrence**
first, made detectable by the **observations-ledger** (delta):

- Search the ledger issue's comments alongside the open-ticket dedup above. A
  matching ledger entry means this IS the second occurrence: file the
  standalone ticket with both occurrences in `## Occurrences`, and reply to
  the ledger entry with `graduated → #<M>` so later matches point at the
  ticket instead of re-recording.
- No match → record, don't file. Append one comment to the ledger —
  `Observation: <area>: <one-line rule> — PR #<N>, <file:line>, class A|B,
  target <path>. — <role>` — and move on. If the repo supplies no
  **observations-ledger** delta, skip recording (note the observation in the
  final Report block only); do not create a ledger yourself.

Recording costs one comment, adds zero tickets to the backlog, and makes
"second occurrence" provable by citation rather than asserted from memory.
This is the anti-false-positive companion to the Step 2 one-off gate.
Multi-occurrence and `Recurred:`-evidenced items always file.

- **No match** (and the gate above passes — a measured misfire, or a
  ledger-proven second occurrence) → file a new ticket,
  tagging **only** `fleet:coding-improvement`
  (write the body to a worktree-local file and pass `--body-file` to avoid
  command-substitution hazards, mirroring `file-epic`; use
  `.coding-improvement-body.md` in the worktree root — NOT `/tmp/` — Claude
  Code's sandbox blocks writes outside the worktree):
  ```
  gh issue create --repo <repo> --label "fleet:coding-improvement" \
    --title "<area>: <short rule> (coding-improvement)" \
    --body-file .coding-improvement-body.md
  ```
  Body shape:
  ```markdown
  **Class:** A (missing rule) | B (exists but didn't fire)
  **Target artifact:** `<path to the doc / rule / check to change>`
  **Proposed change:** <the one-line rule to add or relocate>

  ## Context
  Surfaced fixing feedback on PR #<N>. The reviewer/human flagged
  <one-line of the original concern> (<file:line>).

  ## Why it generalizes
  <one paragraph: the class of mistake this prevents across the fleet,
  and — for Class B — why the existing surface didn't catch it at
  authoring time>

  ## Occurrences
  - PR #<N>, <file:line> — <one-line>
  ```

**Do not** add `human:approved` or `fleet:queued` — `fleet:coding-improvement`
is a *classification* tag (an explicit exception to the "file issues with no
labels" rule), and the ticket is left **un-queued for human triage**. Many
targets are gated self-config (role docs, skills) that no worker may
auto-edit, so leaving it un-queued is what keeps the ticket safe: the human
decides whether and how it gets worked.

Then return to the feedback flow (there is nothing more to do on the PR).

## Step 5 — Close the ticket yourself when the rule lands

A filed ticket sometimes resolves before triage ever sees it: the proposed
rule lands in the target artifact via a **merged** PR — your own AMEND, a
sibling's, or a doc PR that wrote the rule for other reasons. A worker who
verifies that landing may close the ticket directly (`gh issue close <M>
--reason completed`) instead of leaving it to accrue triage budget, when
**all three** hold:

1. **The landing PR is merged** — an open PR is a plan, not a landing.
2. **The rule lives in the ticket's named target artifact** (read the
   file on `master` and cite the location). Landed somewhere *else* is a
   placement question — triage's call, not yours.
3. **The landed text covers the ticket's proposed change** — the same
   rule, not an adjacent or narrower one. A partial landing gets a
   comment, not a close.

The closing comment cites the merged PR, the commit, and the landed
location, so the triage pass's closed-loop check can treat it exactly
like any other closed-as-completed ticket. When any of the three is
arguable, comment "appears landed via #N — suggest closing" and leave the
ticket open; triage adjudicates.

(Why this exists: the first backlog drain found two of its 54 tickets
already landed — each had a worker comment saying so, but no allowance to
close, so both still cost sweep-and-verify budget.)

---

## Report

One short block back to the caller:
- per checklist item: covered? generalizable?
- for each generalizable item: class (A/B), target artifact, and its
  disposition — filed `#<M>`, commented on an existing `#<M>`, or recorded a
  ledger observation.
- if nothing qualified, say so explicitly ("all feedback was one-off /
  subjective; no coding-improvement filed").
