# PR body templates

Canonical `--body` content for `commit-and-push` step 8. Fleet stack and cursor
stack modes apply a delta to the canonical template.

## Canonical template (single-PR mode)

```
## Summary
- <one bullet per key change>

## Test plan
- [ ] <verification steps>

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
line and issue N has a `## Plan` with `### Acceptance criteria`; omit it
otherwise (`Issue: (none)`, plan-less issues). One row per criterion —
authoring rules, the unverifiable-on-this-host convention, and the
fails-means-not-done rule live in
[`docs/agents/AUTHOR-PIPELINE.md`](../../../../docs/agents/AUTHOR-PIPELINE.md)
§ "Acceptance evidence". The two sections answer different questions:
`## Test plan` says how you verified the code doesn't break; `## Acceptance
evidence` proves the ticket's named criteria actually fired.

## Fleet stack delta

Insert after `## Summary`, before `## Test plan`:

```
## Stack context
This PR is part of a stack. Reviewers: review this PR on its own;
the chain is coordinated in the PR body's "Stacked on" line.

Stacked on: <previous PR URL, or "master" for the first PR>
Full chain: #<A>, #<B>, #<C>
```

Drop `## Notes for reviewer`. The `Closes #<issue-N>` line is already in the
canonical template above — keep it as written.

## Cursor stack delta

Insert after `## Summary`, before `## Test plan`:

```
## Stack context
Stacked on: $parent_pr_ref

When the parent PR merges, change this PR's base to `master`
(via the GitHub UI or `gh pr edit <N> --base master`).
```

No `Full chain:` line. **Drop the `Closes #<issue-N>` line** — the parent PR
closes the shared issue when it merges to master. Avoid duplicating `Closes #N`
on the child while the parent is still in review. The parent PR (which targets
master directly via the canonical template) carries the `Closes` line and closes
the shared issue when it merges first. Drop `## Notes for reviewer`.
`$parent_pr_ref` is the parent PR URL (or branch name if no PR exists yet). Use `<<EOF` (no quotes) in the HEREDOC so that `$parent_pr_ref` expands; `<<'EOF'` suppresses expansion and embeds the literal string.
