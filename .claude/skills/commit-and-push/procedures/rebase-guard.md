# Rebase guard

**Before rebasing this PR branch onto origin/master**, always capture the current diff first. Git's 3-way merge can silently drop hunks from non-conflicting regions of a file when a different region of the same file has a conflict — no conflict markers, no warning in the rebase output.

Do NOT use `>` redirects to `/tmp/` (or any path) — Claude Code's Bash tool blocks shell redirects regardless of destination. Both snapshots live in your conversation context as Bash output; large diffs auto-persist to a `<persisted-output>` link the next iteration can Read. (Same rule that `role-merger.md` uses for its rebase guard.)

## Pre-capture (do this BEFORE `git rebase origin/master`)

Run `git diff origin/master` and keep the output in your conversation context — you'll compare it to the post-rebase snapshot below.

## Rebase and resolve conflicts

Run `git rebase origin/master`. Resolve any conflict markers normally.

## Post-capture and comparison

Run `git diff origin/master` again. Compare to the pre-capture above: look for lines beginning with `+` in the pre-capture that are absent from the post-capture. Each such gap is a silently dropped hunk that must be manually re-applied before committing.

For huge diffs that don't fit cleanly in context, both snapshots get auto-persisted by Claude Code; Read the persisted-output files to diff them with the Read tool's `offset`/`limit`.

## If the pre-capture was skipped

1. Run `git reflog --since=2.hours.ago` to confirm a rebase happened.
2. Compare `git diff origin/master` against the branch's last pushed state: `git diff origin/<branch-name>` shows what changed since the last push.
3. Look for missing code blocks based on the PR's commit messages and description. Re-apply any that are absent.
