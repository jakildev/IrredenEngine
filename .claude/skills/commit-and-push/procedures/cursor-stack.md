# Cursor stack mode

The cursor-flow analog of [fleet stack mode](fleet-stack.md): one PR per
slice with `--base <previous-feature-branch>` instead of `--base master`.
No `fleet-claim` machinery, no task IDs, no stack label; the state is the
per-branch git config `start-next-task` writes.

## Detection (after fleet stack mode is ruled out)

```bash
git config --get branch.$(git branch --show-current).cursor-stack-base
```

Empty / exit 1 → normal single-PR flow. A branch name → cursor-stacked on
it.

## Deltas

- Step 8 `--base` is the recorded `cursor-stack-base`.
- After the PR opens, run [native-stack-link.md](native-stack-link.md)
  with that base and the new PR number. If the parent branch has no open
  PR yet, the link step skips itself — warn and re-run it once the parent
  PR exists.
- Title keeps the cursor-flow shape (no issue-number prefix); no extra
  labels.
- Leave the `cursor-stack-base` config in place afterwards as the chain
  record; a non-stacked branch takes the standard path automatically.
  When the parent merges, GitHub retargets and rebases this PR
  server-side; merging from the stack UI pulls unmerged parents in
  bottom-up.

On macOS, Cursor's sandbox blocks `gh` keychain access and SSH pushes —
run `gh pr create|edit|list` and `git push` with the `all` permission.
