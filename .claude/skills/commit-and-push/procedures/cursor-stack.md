# Cursor stack mode

This is the cursor-flow analog of the [fleet stack mode](fleet-stack.md). It opens a single PR per slice but with `--base <previous-feature-branch>` instead of `--base master`, so the diffs stay isolated while the chain accumulates. No `fleet-claim` machinery, no task IDs, no `fleet:stacked` label. State lives entirely in the per-branch git config that `start-next-task` writes when the human cues stacking.

## Detection

Detect cursor stack mode after step 1 of the main flow, AFTER ruling out fleet stack mode:

```bash
git config --get branch.$(git branch --show-current).cursor-stack-base
```

- Output is empty / exit 1 → not cursor-stacked. Proceed with the normal single-PR flow in [`SKILL.md`](../SKILL.md).
- Output names a branch (e.g. `claude/render-glow-pulse`) → the current branch is cursor-stacked on that branch. Note the value; step 8 uses it as `--base` and the post-open link step registers the native stack.

## Deltas vs. the single-PR flow

- **Step 8 PR base** is the recorded `cursor-stack-base` instead of `master`. Pass it to `gh pr create` as `--base <base>`.
- **After the PR opens**, link it into the native GitHub stack: run the [native-stack-link.md](native-stack-link.md) step with the recorded `cursor-stack-base` as `$base` and the new PR number. No `Stacked on:` body line — the stack badge on the PR header is the chain record. If the parent branch has no open PR yet (the human hasn't run `commit-and-push` on it — unusual but possible), the link step skips itself; warn the user and re-run the link after the parent PR exists.
- **Title** uses the normal cursor-flow shape (no issue-number prefix — cursor flow branches are named `claude/<area>-<topic>`).
- **No labels** beyond what the normal flow adds. The `fleet:stacked` label is fleet-only; cursor flow relies on the native stack object alone.

## After the PR opens

Do NOT clear the `cursor-stack-base` config — leave it as a record of the chain. The config is local-only and doesn't need cleanup; the next `commit-and-push` on a non-stacked branch (no config set) takes the standard path automatically.

When the parent PR merges, GitHub re-targets and rebases this PR onto `master` server-side — no manual `gh pr edit --base` step. Merging this PR from the stack UI pulls any unmerged parents in with it (coupled bottom-up merge), so the human can also just merge the top of whatever sub-chain is ready.

## macOS sandbox note

Cursor's Bash sandbox blocks `gh` keychain access and SSH `git push`. Always run `gh pr create`, `gh pr edit`, `gh pr list`, and `git push` with the `all` permission on macOS. Reads of `git config --get …` are not sandboxed.
