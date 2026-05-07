# Cursor stack mode

This is the cursor-flow analog of the [fleet stack mode](fleet-stack.md). It opens a single PR per slice but with `--base <previous-feature-branch>` instead of `--base master`, so the diffs stay isolated while the chain accumulates. No `fleet-claim` machinery, no task IDs, no `fleet:stacked` label. State lives entirely in the per-branch git config that `start-next-task` writes when the human cues stacking.

## Detection

Detect cursor stack mode after step 1 of the main flow, AFTER ruling out fleet stack mode:

```bash
git config --get branch.$(git branch --show-current).cursor-stack-base
```

- Output is empty / exit 1 → not cursor-stacked. Proceed with the normal single-PR flow in [`SKILL.md`](../SKILL.md).
- Output names a branch (e.g. `claude/render-glow-pulse`) → the current branch is cursor-stacked on that branch. Note the value; step 8 uses it as `--base` and writes a `Stacked on:` line to the PR body.

## Deltas vs. the single-PR flow

- **Step 8 PR base** is the recorded `cursor-stack-base` instead of `master`. Pass it to `gh pr create` as `--base <base>`.
- **PR body** includes a `Stacked on: <PR URL>` line. Look up the parent PR URL once at PR-open time:
  ```bash
  parent_branch=$(git config --get branch.$(git branch --show-current).cursor-stack-base)
  parent_pr_url=$(gh pr list --head "$parent_branch" --state all --json url -q '.[0].url' --limit 1)
  ```
  If the parent has no PR yet (e.g. the human hasn't run `commit-and-push` on it — unusual but possible), use the branch name instead: `Stacked on: <parent_branch>` and warn the user.
- **Title** uses the normal cursor-flow shape (no `T-NNN:` prefix — cursor flow doesn't use the queue).
- **No labels** beyond what the normal flow adds. The `fleet:stacked` label is fleet-only; cursor flow just relies on `Stacked on:` in the PR body.

## After the PR opens

Do NOT clear the `cursor-stack-base` config — leave it as a record of the chain. The config is local-only and doesn't need cleanup; the next `commit-and-push` on a non-stacked branch (no config set) takes the standard path automatically.

When the parent PR merges, change this PR's base to `master` in the GitHub UI (or `gh pr edit <N> --base master`) — same step as in any manual stacked-PR workflow.

## macOS sandbox note

Cursor's Bash sandbox blocks `gh` keychain access and SSH `git push`. Always run `gh pr create`, `gh pr edit`, `gh pr list`, and `git push` with the `all` permission on macOS. Reads of `git config --get …` are not sandboxed.
