---
description: >-
  Push changes on the current PR branch and request a fleet re-review.
  Use when the user says "request re-review", "push and re-review",
  "push for re-review", "I'm done with this PR have the fleet
  re-review", or "update PR and get it reviewed again". Also useful
  when the user has manually edited a PR branch in any pane and wants
  the fleet review pipeline to pick it back up.
---

# request-re-review

Push changes on the current PR branch and request a fleet re-review.

## When to invoke

Trigger when the user says:

- "request re-review" / "re-review this PR"
- "push and re-review" / "push for re-review"
- "I'm done with this PR, have the fleet re-review"
- "update PR and get it reviewed again"
- Any phrase implying: I've made changes to an approved/reviewed PR
  and want the fleet reviewers to look at it again.

Also useful when the user has checked out a PR branch in any pane
(architect, worker, etc.), made manual edits with an agent, and wants
the review pipeline to pick it back up.

## Preconditions

1. You are on a PR branch (not a scratch branch, not master).
2. There is an open PR for this branch.
3. `gh` is authenticated.

## Flow

### 1. Identify the current PR

```bash
git branch --show-current
```

Find the open PR for this branch:
```bash
gh pr list --state open --head <branch-name> --json number,title,labels
```

If no PR is found, tell the user and stop.

### 2. Commit and push any uncommitted changes

Check for uncommitted work:
```bash
git status --porcelain
```

If there are changes:
- Stage specific files by path (e.g. `git add engine/.../foo.cpp`).
  Do NOT use `git add -A` or `git add .` — risks staging secrets,
  build artifacts, or unrelated stray edits in the worktree.
  `commit-and-push` enforces the same rule; this skill follows it.
- Commit with a descriptive message:
  `git commit -m "changes from human review session"`
- Push: `git push`

If there are no uncommitted changes but there ARE unpushed commits,
push them.

If everything is already clean and pushed, that's fine — proceed to
relabel.

### 3. Swap labels to request re-review

Remove stale fleet verdict labels and add the re-review request:
```bash
gh pr edit <N> --remove-label "fleet:approved" --remove-label "fleet:needs-fix" --remove-label "fleet:blocker" --add-label "human:re-review"
```

### 4. Post a comment

```bash
gh pr comment <N> --body "Human made changes — re-review requested."
```

### 5. Release the branch

Determine the correct scratch branch name based on your worktree:
- If in an engine worktree named `<name>`: `claude/<name>-scratch`
  (e.g., `opus-architect` → `claude/opus-arch-scratch`)
- If unsure, use a generic: `claude/scratch`

Reset to the scratch branch:
```bash
git checkout -B claude/<name>-scratch origin/master
```

This frees the PR branch so reviewers (or other agents) can check
it out.

### 6. Report back

Print:
```
Re-review requested for PR #<N> (<title>).
- Label: human:re-review added, fleet:approved removed
- Branch released — reviewers can check it out
- Next reviewer pass will pick it up
```

## Anti-patterns

- ❌ Leaving the PR branch checked out after requesting re-review.
  The whole point is to release it for reviewers.
- ❌ Using this on a PR that's still `fleet:wip` — that PR isn't
  ready for review at all. Tell the user to finalize first.
- ❌ Force-pushing. Never `--force`.
