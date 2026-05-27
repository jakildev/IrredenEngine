# Fleet stack mode

This is the **fleet stack** path. The cursor-flow stacking variant lives in [`cursor-stack.md`](cursor-stack.md); they have separate detection signals and are mutually exclusive in practice.

If the current task is part of an `fleet-claim stack` chain (i.e. the caller's worktree name has a stack claim under `~/.fleet/claims/_stack_<agent>/`), this skill opens **one PR per task, chained by `--base`** instead of a single PR with multiple commits.

## Detection

Detect stack mode at the start of the flow:

```bash
fleet-claim stack-pr-state <your-worktree-name>
```

- Output `no stack claim for agent: <name>` → not stacked, proceed with the normal single-PR flow in [`SKILL.md`](../SKILL.md).
- Output with `task`/`branch`/`pr` columns → stacked. The row whose PR column is `(pending)` and whose earlier rows (if any) are all filled is the current task. Note its `<task-id>`; you will need it in steps 2 and 8 of the main flow.

## Deltas vs. the single-PR flow

- **Step 2 branch name** is `claude/<issue#>-<short-topic>` (e.g. `claude/1234-occupancy-grid`). The issue-number prefix lets reviewers and `stack-base` resolve the chain.
- **Step 8 PR base** is `fleet-claim stack-base <agent> <task-id>` instead of `master`. For the first task this still returns `master`; for subsequent tasks it returns the previous task's branch.
- **After `gh pr create`** record the PR in the stack so the next task can chain off it:
  ```bash
  fleet-claim stack-set-pr <agent> <task-id> <branch> <pr-url>
  ```
- **PR body** includes a `## Stack context` block with a `Stacked on:` line (the previous PR URL, or `master` for the first task in the chain) and a `Full chain:` line listing the task IDs the molecule covers. Reviewers use this to navigate sibling PRs without leaving the diff.
- **Labels** include `fleet:stacked` whenever `--base != master` (i.e. every PR in the chain except the first). The merger reads `baseRefName` directly for routing decisions; the label is a derived convenience for human visibility and cheap GitHub-side filtering.
- **Title** starts with a scope prefix per the commit-message style guide; the issue number goes in the `Closes #N` line so reviewers can trace the chain.

## After the PR opens

Do NOT start the next task in the same branch. Invoke `start-next-task` the same way as single-PR work; when it comes back, the next stacked-PR iteration computes its own `--base` via `stack-base` and branches off that (not `origin/master`).

When **the final task's PR is merged**, run `fleet-claim release-stack <agent>` to clean up both the per-task claims and the stack metadata.
