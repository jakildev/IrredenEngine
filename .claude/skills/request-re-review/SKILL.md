---
name: request-re-review
description: >-
  Pushes changes on the current PR branch, swaps the verdict labels for
  human:re-review, and releases the branch so the fleet review pipeline
  picks it back up. Use when the user says "request re-review", "push and
  re-review", "push for re-review", "I'm done with this PR have the fleet
  re-review", or "update PR and get it reviewed again", including after
  hand-edits to a PR branch in another pane.
---

# request-re-review

Preconditions: on a PR branch (not `master`, not a scratch branch) with an
open PR; `gh` authenticated. A PR still labelled `fleet:wip` is not ready
for review — tell the user to finalize first.

1. Identify the PR:
   ```bash
   git branch --show-current
   gh pr list --state open --head <branch-name> --json number,title,labels
   ```
   No PR → tell the user and stop.
2. Invoke `commit-and-push` for any uncommitted or unpushed changes.
   Never `--force`.
3. Swap labels:
   ```bash
   gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --add-label "human:re-review"
   ```
4. `gh pr comment <N> --body "Human made changes — re-review requested."`
5. Invoke `start-next-task` to move to a scratch branch off
   `origin/master` — the PR branch must be released so reviewers can
   check it out.
6. Report:
   ```
   Re-review requested for PR #<N> (<title>).
   - Label: human:re-review added, fleet:approved removed
   - Branch released — reviewers can check it out
   - Next reviewer pass will pick it up
   ```
