---
name: start-next-task
description: >-
  Reset the current worktree to a fresh feature branch off the latest
  origin/master so the agent can start the next chunk of work without
  contaminating the previous PR branch. Use after commit-and-push has opened a
  PR and the user (or you) wants to move on to the next task, OR whenever the
  user says "next task", "start next", "move on", or "pull master and start
  fresh". This is the "rebase-off-master-after-opening-a-pr" step of the
  parallel-agent workflow.
---

# start-next-task

Cleanly transitions a worktree from "this chunk is done, PR is open" to
"ready for the next chunk of work on fresh master". This is the other half
of the new PR workflow — `commit-and-push` packages a slice; `start-next-task`
prepares the worktree for the next slice.

## When to invoke

Trigger when the user says:

- "next task" / "start next" / "what's next" (when a PR has just been opened)
- "move on" / "move to the next item"
- "pull master and start fresh"
- "rebase off master"
- Immediately after `commit-and-push` completes, if the user indicated they
  want to keep working (e.g. "commit this and then start on the pathfinding
  refactor").

Do **not** invoke proactively without a cue. If the user says "commit and
push" and stops, don't also run `start-next-task` — wait for them to ask.

## Preconditions

1. The working tree should be **clean**. If `git status` shows uncommitted
   changes, stop and warn — either they belong in the previous PR (go back
   to `commit-and-push`) or they are a WIP the user wants to keep.
2. You should be on a feature branch whose PR is **already open and pushed**.
   If the current branch has unpushed commits, stop and warn — those commits
   would be stranded by the branch switch.
3. `gh` authenticated.

## Flow

### 1. Record what branch you were on

```bash
git rev-parse --abbrev-ref HEAD
```

Remember this — you'll reference it in the report at the end, and the user
may want to check that the PR is still tracking it.

### 2. Confirm the old branch's PR is pushed and open

```bash
gh pr list --head <old-branch-name> --state open --json number,url,title
```

- If **one PR** comes back: good, the previous slice is properly filed for
  review. Note the number and URL for the report.
- If **zero PRs** come back: stop and warn the user. Either the previous
  `commit-and-push` didn't finish, or the PR was already closed. Do not
  switch branches until the user tells you what to do — the uncommitted or
  un-PR'd work would be lost.
- If **multiple PRs** come back: unusual. Note all of them in the report and
  continue.

### 3. Fetch latest origin

```bash
git fetch origin master
```

Never rely on a stale local `master` ref. Always fetch before branching.

### 4. Derive a new branch name

Ask the user what the next task is if they haven't already told you. Derive
a short, kebab-case branch name from the task, prefixed `claude/<area>-`:

- `claude/game-ant-pheromones`
- `claude/engine-velocity-drag-refactor`
- `claude/render-lod-threshold-tuning`

If the user already gave you a task and a name is obvious, just use it. Do
not pick a name with a random suffix — permanent worktrees work best with
human-readable, topic-named branches.

### 5. Discard any staged or working-tree changes from the old branch

Before switching, ensure the working tree is fully clean — even of files
that look like a no-op (e.g. `TASKS.md` that was checked out from
`origin/master` to read the latest queue while you were on the feature
branch — that staging blocks the next branch checkout):

```bash
git restore --staged .
git checkout -- .
```

If `git status` reported clean in the preconditions, these are no-ops.
If it didn't, these clear any leftover staged changes that would otherwise
fail the next `git checkout -b` with "your local changes would be
overwritten by checkout."

### 6. Check out the new branch off fresh origin/master

```bash
git checkout -b claude/<new-area>-<new-topic> origin/master
```

This creates the new branch starting from the tip of `origin/master`, not
from wherever your previous branch was. Critical: without `origin/master`,
you'd branch off your old PR branch and carry its commits forward.

**Do NOT** `git rebase origin/master` on the old branch and keep working on
it. That would mix old PR commits with new work, which pollutes the old PR
when you push. Always start a new branch.

### 7. Sanity-check the state

```bash
git status
git log --oneline -5
```

- `git status`: should be `nothing to commit, working tree clean`.
- `git log --oneline -5`: the top commit should now be the latest `master`
  commit, not one of your previous PR's commits. If it's still showing old
  PR commits, the checkout went wrong — stop and investigate.

### 8. Read the relevant CLAUDE.md for the new task area

Before starting the next task, read the most specific `CLAUDE.md` for the
directory you're about to work in. For example:

- Task in `engine/render/` → read `engine/render/CLAUDE.md`.
- Task in `engine/prefabs/irreden/update/` → read that file's CLAUDE.md.
- Task inside a creation subdirectory → read that creation's own
  `CLAUDE.md` (creations layered on top of the engine define their own
  conventions, review criteria, and sometimes workflows).

This primes your context with the module's conventions and gotchas before
you start editing. If the subdirectory has a dedicated workflow that
differs from the engine baseline, honor the subdirectory's rules.

### 9. Report

Reply with a compact summary:

- Old branch name + its PR URL (from step 2).
- New branch name + confirmation it's based on fresh `origin/master`.
- The CLAUDE.md files you just read to prime context.
- One sentence: "Ready for <next task>. Go ahead."

## Anti-patterns

- ❌ Switching branches with a dirty working tree. Always clean first.
- ❌ Branching off your previous PR branch instead of `origin/master`. This
  stacks work and pollutes the old PR.
- ❌ Running `git rebase origin/master` on the old branch to "catch it up",
  then reusing it. Rebasing the same branch for new unrelated work is how
  PR histories become unreadable.
- ❌ Deleting the old local branch. Leave it alone — if the reviewer asks
  for changes, you'll need to check it out again.
- ❌ Starting the next task without reading the target area's CLAUDE.md.
  That's where the module-specific invariants live.
- ❌ Invoking this skill when no PR was actually opened (i.e. after a
  `commit-and-push` failure). Check step 2.

## Recovery

If you discover after switching branches that the old branch had uncommitted
work (somehow step 1's clean check missed it):

1. `git checkout <old-branch>`
2. Stash or commit the dirty work on the old branch.
3. Push and update the PR.
4. `git checkout <new-branch>` to return.
