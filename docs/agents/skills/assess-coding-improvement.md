# assess-coding-improvement — shared flow

After a worker has fixed the feedback on a PR: confirm every comment was
covered, then decide whether the fix reveals a **generalizable**
improvement to the fleet's conventions (style guide, coding rules, the
`simplify` checks, the review checklist, worker direction) and, if so,
file — or append to — a `fleet:coding-improvement` ticket so the mistake
is caught at authoring time next time.

Invoked on explicit ask only; the feedback path does not run it. A
reflection pass only: it never touches the PR's code, labels, or claim.
The backlog it produces is drained by
[`triage-coding-improvements.md`](triage-coding-improvements.md).

Each repo's `.claude/skills/assess-coding-improvement/SKILL.md` is a thin
wrapper that points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)).

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

---

## When to run

- Only on explicit ask ("should this be a fleet rule?", "assess coding
  improvement", "file a coding-improvement"). The feedback AMEND path does
  not invoke it.
- File only when the improvement is a validator — a check, ratchet, lint,
  or test the fleet can execute — and the defect fired (a wrong merge, a
  wasted iteration, a reviewer catch on a real defect). A prose rule, a
  reminder, or a style preference is not filed (FLEET.md §"Improvement
  posture").

## Step 1 — Coverage

Re-run the **comments tool** and build a checklist with one item per
output line — every `[comment …]`, `[review …]` summary, and
`[path:line]` inline comment. Confirm the pushed fix addresses each (or
that the Step-e summary / ESCALATE issue accounts for it). An uncovered
item means going back to address it before reflecting.

## Step 2 — Generalizable?

Carry forward only items that are an instance of a repeatable pattern — a
naming slip, a math primitive that bypassed the engine math layer, a
per-entity lookup in a tick or an allocation in a hot loop, an ECS
ordering/ownership footgun, a doc-comment or test-coverage standard, a
render-pipeline invariant. One-offs (a wrong magic number, an off-by-one,
a typo, a wording tweak) produce nothing — this is the anti-spam gate.

## Step 3 — Classify

Search the **convention surfaces** for the rule, then classify:

- **(A) Missing** — no surface states it. Add it to the authoritative
  surface for its class.
- **(B) Exists but didn't fire** — the defect is surfacing, not text. Move
  the rule to where the worker hits it at authoring time: a check on the
  **automated-check surface** when mechanically detectable (the strongest
  lever); a relocation or cross-link into the doc the worker reads at the
  relevant moment; the **review checklist** as a backstop, in addition to
  one of those.

The ticket names the class, the exact target artifact (file path), and
the one-line rule.

## Step 4 — Dedup, then comment-or-file

```
gh issue list --repo <repo> --label fleet:coding-improvement --state open \
  --json number,title,body
```

- **Open ticket targets the same artifact/rule** → add an occurrence:
  ```
  gh issue comment <M> --repo <repo> --body "Recurred: PR #<N>, <file:line> — \
  <one-line>. — <role-name>"
  ```
- **First occurrence** → the second-occurrence-or-measured-misfire gate. A
  standalone ticket files on a first occurrence only for a **measured
  misfire**: a near-miss correctness/safety defect observed in something
  you ran (a wrong result, a false-clean pass, a defect that would have
  shipped) — not a failure mode predicted from reading source. Everything
  else needs a second occurrence, proved by the **observations-ledger**:
  - ledger match → this is the second occurrence: file, list both in
    `## Occurrences`, and reply to the ledger entry `graduated → #<M>`;
  - no match → append one ledger comment and move on: `Observation:
    <area>: <one-line rule> — PR #<N>, <file:line>, class A|B, target
    <path>. — <role>`. Without a ledger delta, note the observation in the
    Report only; never create a ledger.
- **Filing** (measured misfire, or ledger-proven second occurrence): write
  the body to `.coding-improvement-body.md` in the worktree root (not
  `/tmp/`), then
  ```
  gh issue create --repo <repo> --label "fleet:coding-improvement" \
    --title "<area>: <short rule> (coding-improvement)" \
    --body-file .coding-improvement-body.md
  ```
  ```markdown
  **Class:** A (missing rule) | B (exists but didn't fire)
  **Target artifact:** `<path to the doc / rule / check to change>`
  **Proposed change:** <the one-line rule to add or relocate>

  ## Context
  Surfaced fixing feedback on PR #<N>. The reviewer/human flagged
  <one-line of the original concern> (<file:line>).

  ## Why it generalizes
  <the class of mistake this prevents; for Class B, why the existing
  surface didn't catch it at authoring time>

  ## Occurrences
  - PR #<N>, <file:line> — <one-line>
  ```

`fleet:coding-improvement` is the only label — a classification tag, and
the sanctioned exception to filing issues unlabelled. Never add
`human:approved` or `fleet:queued`: most targets are gated self-config
that no worker may edit, so the ticket waits for human triage.

## Step 5 — Close it yourself when the rule lands

Close a ticket (`gh issue close <M> --reason completed`) when all three
hold: the landing PR is **merged**; the rule lives in the ticket's named
target artifact on the default branch (cite the location); the landed text
covers the proposed change, not an adjacent or narrower one. The closing
comment cites PR, commit, and location. If any is arguable, comment
"appears landed via #N — suggest closing" and leave it to triage.

---

## Report

One block: per checklist item, covered? generalizable?; per generalizable
item, class, target artifact, disposition (filed `#<M>`, commented on
`#<M>`, or ledger observation); or "all feedback was one-off / subjective;
no coding-improvement filed."
