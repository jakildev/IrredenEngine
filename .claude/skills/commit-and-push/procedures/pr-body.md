# PR body templates

Canonical `--body` content for `commit-and-push` step 8. Fleet stack and cursor
stack modes apply a delta to the canonical template.

## Canonical template (single-PR mode)

```
## Summary
- <one bullet per key change>

## Test plan
- [x] <verification steps — written from observed output, ticked>

## Acceptance evidence
| Criterion | Check run | Observed |
|---|---|---|
| <criterion from the plan> | `<command>` | <output line proving it fired> |

## Notes for reviewer
<optional — include only when the reviewer needs specific guidance>

Closes #<issue-N>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

Omit `## Notes for reviewer` when there is nothing non-obvious to call out.
Omit the `Closes #<issue-N>` line when the task's `Issue:` field is `(none)`
(e.g. cleanup PRs, fleet-tooling PRs filed without a tracking issue).

The `Closes #N` line is what makes GitHub auto-close the originating issue on
merge. Always include it when an Issue number exists.

`## Acceptance evidence` is required whenever the body carries a `Closes #N`
line and issue N states acceptance criteria **anywhere** — a `## Plan`
comment's `### Acceptance criteria` section OR the issue body itself (the
`fleet:no-plan` agent-approved lane carries them in the body under a bold
`**Acceptance criteria**` line, never a `## Plan` comment, #2521); omit it
otherwise (`Issue: (none)`, issues stating no criteria). One row per
criterion — authoring rules, the unverifiable-on-this-host convention, and
the fails-means-not-done rule live in
[`docs/agents/AUTHOR-PIPELINE.md`](../../../../docs/agents/AUTHOR-PIPELINE.md)
§ "Acceptance evidence". The two sections answer different questions:
`## Test plan` says how you verified the code doesn't break; `## Acceptance
evidence` proves the ticket's named criteria actually fired. `## Test plan`
is past tense — a record of verification already run, each item written
from output you observed, box ticked. An unticked `- [ ]` box means the PR
is not ready to leave WIP: either run the check and tick it from the
observed output, or delete the item (#2658).

## Fleet stack delta

No body block. Stack membership, chain navigation, and merge sequencing all
live in the native GitHub stack (the [native-stack-link.md](native-stack-link.md)
step registers it after PR open; the PR header's stack badge shows the
chain). Never write `Stacked on:` or `Full chain:` lines — the legacy body
markers went stale after retargets and misrouted review (#2231).

Drop `## Notes for reviewer`. The `Closes #<issue-N>` line is already in the
canonical template above — keep it as written (each fleet-stack task has its
own issue).

## Cursor stack delta

No body block either — same native-stack rule as the fleet stack delta.

**Drop the `Closes #<issue-N>` line** — cursor-stack slices usually share
one issue, and the parent PR (which targets master directly via the
canonical template) carries the `Closes` line. Avoid duplicating `Closes #N`
on the child while the parent is still in review. Drop `## Notes for
reviewer`.
