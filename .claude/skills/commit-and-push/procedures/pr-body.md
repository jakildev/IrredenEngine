# PR body templates

Canonical `--body` content for `commit-and-push` step 8. Fleet stack and cursor
stack modes apply a delta to the canonical template.

## Canonical template (single-PR mode)

```
## Summary
- <one bullet per key change>

## Test plan
- [ ] <verification steps>

## Notes for reviewer
<optional — include only when the reviewer needs specific guidance>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

Omit `## Notes for reviewer` when there is nothing non-obvious to call out.

## Fleet stack delta

Insert after `## Summary`, before `## Test plan`:

```
## Stack context
This PR is part of a stack. Reviewers: review this PR on its own;
the chain is coordinated in the PR body's "Stacked on" line.

Stacked on: <previous PR URL, or "master" for the first PR>
Full chain: T-<A>, T-<B>, T-<C>
```

Also add `Closes #<issue-N>` before the robot footer. Drop `## Notes for reviewer`.

## Cursor stack delta

Insert after `## Summary`, before `## Test plan`:

```
## Stack context
Stacked on: $parent_pr_ref

When the parent PR merges, change this PR's base to `master`
(via the GitHub UI or `gh pr edit <N> --base master`).
```

No `Full chain:` line and no `Closes #<issue-N>`. Drop `## Notes for reviewer`.
`$parent_pr_ref` is the parent PR URL (or branch name if no PR exists yet). Use `<<EOF` (no quotes) in the HEREDOC so that `$parent_pr_ref` expands; `<<'EOF'` suppresses expansion and embeds the literal string.
