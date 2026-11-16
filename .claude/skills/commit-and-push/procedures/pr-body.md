# PR body templates

Canonical `--body-file` content for `commit-and-push` step 8.

## What the page is for

The owner reads a PR page to decide whether to merge; a reviewing agent
reads it to check the proof. Write for the first reader and keep the
second one's detail one click away. These rules hold for the body, for a
review ([`review-pr`](../../../../docs/agents/skills/review-pr.md) step 5)
and for every follow-up comment:

- **State each fact once.** A reading in the evidence table is not repeated
  in the summary, a note or a later comment. A review does not restate the
  PR, and an amend comment does not restate the review.
- **Outcome, not inventory or history.** Say what behaves differently after
  the merge and why. The diff lists the files; the commit log has the order
  things happened in.
- **Visible: summary, screenshots, evidence. Collapsed: everything else.**
  Agents read through a `<details>` block; people skip it.
- **A table cell holds one command or one reading.** A row that needs
  explaining gets a line under `## Notes for reviewer`.

A body over about 40 visible lines is carrying something that belongs in
the collapsed block, the issue thread or a committed document.

## Canonical template (single-PR mode)

```
## Summary
<at most five lines: what changes for the engine or its user, and why>

## Screenshots
| Before | After |
|---|---|
| ![before](<url>) | ![after](<url>) |

## Acceptance evidence
| Criterion | Check run | Observed |
|---|---|---|
| <criterion, abbreviated> | `<command>` | <the reading that proves it fired> |

<details>
<summary>Reviewer notes</summary>

## Plan departures
- <one line per plan-vs-tree difference you absorbed: what the plan said, what the tree has, what you did>

## Notes for reviewer
- <only what the diff and the table cannot show>

</details>

Closes #<issue-N>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

- `## Screenshots`: every PR that touches a visual-file glob has this
  section, straight after the summary. The **lead pair** (the shot where
  the change shows best) is visible; any other pairs sit in one
  `<details>` block under it. A change with no visual effect embeds no
  image and says so in one line (`No visual change: 28 of 28 pairs
  identical, IRShapeDebug`). Snippet and rules: the screenshot skill's
  step 8. Omit the section for a PR that touches no visual path.
- `## Acceptance evidence`: required whenever the body carries `Closes #N`
  and issue N states acceptance criteria anywhere — a `## Plan` comment's
  `### Acceptance criteria` or a bold `**Acceptance criteria**` line in
  the body (the `fleet:no-plan` lane). One row per criterion; a check you
  ran beyond the criteria is one more row. Authoring rules, the
  unverifiable-on-this-host convention, and fails-means-not-done live in
  [`docs/agents/AUTHOR-PIPELINE.md`](../../../../docs/agents/AUTHOR-PIPELINE.md)
  §"Acceptance evidence".
- `## Test plan`: only for a PR with no evidence table. A ticked list of
  the checks already run, each written from observed output (`- [x]
  <command>: <reading>`). Never both sections: the table already is the
  test plan. An unticked `- [ ]` box means the PR is not ready to leave
  WIP: run it and tick it, or delete the item.
- The `<details>` block holds `## Plan departures` (one line per difference
  between the issue's `## Plan` and the tree you built against —
  PLANNING-PROTOCOL.md § "Drift at pickup"; the acceptance grader reads
  it), `## Notes for reviewer`, and any `fleet-pr-overlap` rows the commit
  flow asks you to quote. Drop a heading with nothing under it, and the
  whole block when all are empty.
- `Closes #<issue-N>` and a `Parked-until: #<N>` line stay outside the
  block, each alone on its line. Omit `Closes` when the task's `Issue:`
  field is `(none)`. Write `Refs #N` instead when your own evidence grades
  a criterion as not shipped, so the issue stays open for the residual
  (`scripts/fleet/fleet_scope_shipped.py` reads `Refs` as non-closing).

Before creating a PR or updating an existing PR body, run the executable
`fleet-pr-body-lint` gate from `commit-and-push` step 8a once for every
distinct `Closes #N`. It checks enumeration only; grade meaning remains the
reviewer's job.

## Fleet stack delta

No body block — stack membership, chain navigation, and merge sequencing
live in the native GitHub stack ([native-stack-link.md](native-stack-link.md)).
Never write `Stacked on:` / `Full chain:` lines. Drop `## Notes for
reviewer`; keep `Closes #<issue-N>` (each task has its own issue).

## Cursor stack delta

Same native-stack rule. Drop `## Notes for reviewer` and **drop the
`Closes #<issue-N>` line** — cursor-stack slices usually share one issue,
and the parent PR (targeting master) carries the `Closes` line.
