# PR body templates

Canonical `--body-file` content for `commit-and-push` step 8.

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

- `## Notes for reviewer`: omit when there is nothing non-obvious.
- `Closes #<issue-N>`: omit when the task's `Issue:` field is `(none)`.
  Write `Refs #N` instead when your own `## Acceptance evidence` grades a
  criterion as not shipped, so the issue stays open for the residual
  (`scripts/fleet/fleet_scope_shipped.py` reads `Refs` as non-closing).
- `## Acceptance evidence`: required whenever the body carries `Closes #N`
  and issue N states acceptance criteria anywhere — a `## Plan` comment's
  `### Acceptance criteria` or a bold `**Acceptance criteria**` line in
  the body (the `fleet:no-plan` lane). One row per criterion; authoring
  rules, the unverifiable-on-this-host convention, and fails-means-not-done
  live in [`docs/agents/AUTHOR-PIPELINE.md`](../../../../docs/agents/AUTHOR-PIPELINE.md)
  §"Acceptance evidence". Omit otherwise.
- `## Test plan` records verification already run — each item written
  from observed output and ticked. An unticked `- [ ]` box means the PR
  is not ready to leave WIP: run it and tick it, or delete the item.

## Fleet stack delta

No body block — stack membership, chain navigation, and merge sequencing
live in the native GitHub stack ([native-stack-link.md](native-stack-link.md)).
Never write `Stacked on:` / `Full chain:` lines. Drop `## Notes for
reviewer`; keep `Closes #<issue-N>` (each task has its own issue).

## Cursor stack delta

Same native-stack rule. Drop `## Notes for reviewer` and **drop the
`Closes #<issue-N>` line** — cursor-stack slices usually share one issue,
and the parent PR (targeting master) carries the `Closes` line.
