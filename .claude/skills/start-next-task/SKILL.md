---
name: start-next-task
description: >-
  Reset the current worktree to a fresh feature branch for the next chunk of
  work. In the standard case that means branching off the latest
  origin/master; if the worker has an active fleet-claim molecule (fleet
  stack mode) or the human cued cursor-flow stacking, the new branch
  instead bases on the just-opened PR's head ref so the downstream task's
  diff stays isolated. Use after commit-and-push has opened a PR and the
  user (or you) wants to move on to the next task, OR whenever the user
  says "next task", "start next", "move on", "pull master and start
  fresh", "I merged it", "back to master", "fresh start", "new task", or
  cues stacking with "stack this", "next slice stacked", "keep stacking",
  "stack the next on this PR".
---

# start-next-task

Cleanly transitions a worktree from "this chunk is done, PR is open" to
"ready for the next chunk of work". This is the other half of the new PR
workflow — `commit-and-push` packages a slice; `start-next-task` prepares
the worktree for the next slice.

The skill operates in three modes, selected in priority order at step 4:

- **Fleet stack mode** (active `fleet-claim` molecule with remaining
  tasks): branch off the **old branch** — i.e. the head ref of the PR
  that `commit-and-push` just opened. The downstream task's branch then
  contains the upstream commits so the worker can keep building, while
  its PR's `--base <upstream>` (set by `commit-and-push` at PR-open
  time) keeps the diff scoped to its own changes. Selected via
  `fleet-claim molecule resume`.
- **Cursor stack mode** (cursor flow only, driven by user cue): same
  mechanic as fleet stack mode (branch off the old branch) but no
  molecule machinery. Records the parent branch in
  `branch.<new>.cursor-stack-base` git config so `commit-and-push`
  picks it up later when opening the PR. Selected when the human
  cues stacking ("stack this", "next slice stacked", "keep stacking",
  "stack the next on this PR") and there is no active fleet molecule.
- **Standard mode** (no fleet molecule and no stack cue): branch off
  fresh `origin/master`. The historical behavior and the most common
  case for both fleet single-task work and cursor flow's "I merged it,
  start something new" pattern.

## When to invoke

Trigger when the user says:

- **Fresh-start cues** (→ standard mode):
  - "next task" / "start next" / "what's next" (when a PR has just
    been opened)
  - "move on" / "move to the next item"
  - "pull master and start fresh" / "rebase off master"
  - "I merged it" / "merged, let's keep going" / "back to master"
  - "fresh start" / "new task"
- **Stack cues** (→ cursor stack mode, when not in fleet stack mode):
  - "stack this" / "next slice, stacked" / "keep stacking"
  - "stack the next on this PR"
  - "build on the last PR" / "PR-stacked next slice"
- Immediately after `commit-and-push` completes, if the user indicated
  they want to keep working (e.g. "commit this and then start on the
  pathfinding refactor", or "ship it and keep going stacked").

Do **not** invoke proactively without a cue. If the user says "commit
and push" and stops, don't also run `start-next-task` — wait for them
to ask. The one cursor-flow exception is documented in the top-level
`CLAUDE.md`: when a new chat lands on a feature branch with an
already-merged PR and the user asks for new work, surface the state
and ask whether to invoke `start-next-task` first.

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

Remember this — you'll reference it in the report at the end, the user may
want to check that the PR is still tracking it, and stack mode (step 4)
uses it as the new branch's base.

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

### 4. Detect mode

Three modes, checked in priority order. The first one that matches wins.

#### 4a. Fleet stack mode

If the current task is part of a `fleet-claim stack` chain, the next
branch must build on the just-opened PR's head ref — not
`origin/master` — so the downstream task's diff shows only its own
changes. Probe the molecule (agents already know their own worktree
name — it is passed in their role instructions, the same way
`fleet-heartbeat <name>` is called):

```bash
fleet-claim molecule resume <your-worktree-name>
```

Interpret the output (the helper always exits 0; discriminate via stdout):

- **stdout names a `T-NNN`** — there is an active stack with remaining
  tasks. The returned ID is the **next** task in the chain (resume marks
  the first pending task as `in-progress` as a side effect).
  - **Sanity-check:** if the returned ID matches the old branch's task
    prefix (e.g. old branch `claude/T-005-foo` and resume returned
    `T-005`), the worker forgot to run `fleet-claim molecule advance
    <agent> T-005 done pr=<URL> commit=<SHA>` after `commit-and-push`.
    Stop and tell the user to advance the molecule before retrying — do
    not proceed, or you will branch back onto the same task.
  - Otherwise the new branch is `claude/<returned-T-NNN>-<short-topic>`
    based on the **old branch** (the just-opened PR's head ref). Set
    `MODE=fleet-stack`, `BASE=<old-branch-name>`, and remember
    `<returned-T-NNN>` for step 5.
- **stdout is empty** — no fleet molecule. Continue to 4b.

If `molecule resume` exits non-zero (malformed YAML, etc.), stop and
surface the stderr message — that is a real fault, not a "no work" signal.

In a cursor-flow session there is no fleet-claim machinery; this probe
will return empty unless the user has set up fleet machinery on the
side. That's the expected case for cursor flow.

#### 4b. Cursor stack mode

If 4a returned empty AND the user's cue contained a stacking phrase
("stack this", "next slice stacked", "keep stacking", "stack the next
on this PR", "build on the last PR"), this is **cursor stack mode**.

Set `MODE=cursor-stack`, `BASE=<old-branch-name>` (the branch you
recorded in step 1). Note the old branch name — step 7b writes it to
git config on the new branch.

If 4a returned empty AND the cue is fresh-start ("next task", "I
merged it", "back to master", etc.), continue to 4c.

If the cue is ambiguous (just "next slice" / "next" with no stacking
or fresh-start hint) AND the **old branch has its own
`cursor-stack-base` set** (i.e. the old branch is itself the middle
of a stack), stop and ask the user:

> You're on a stacked branch (`<old-branch>` → base
> `<existing-stack-base>`). Should the next slice continue the stack
> (branch off `<old-branch>`), or branch off `master`?

Don't guess. Stack continuation has different review semantics than a
fresh slice and the human should pick.

#### 4c. Standard mode

Default. `MODE=standard`, `BASE=origin/master`. Proceed.

### 5. Derive a new branch name

In **fleet stack mode** (4a returned a `T-NNN`): the new branch is
`claude/<T-NNN>-<short-topic>`. Pull the topic from the task title in
`TASKS.md` if obvious (read it via `git show origin/master:TASKS.md` —
do NOT `git checkout origin/master -- TASKS.md`, which would stage it
and break the next `git checkout -b`). If the user already named the
next slice in conversation, prefer that; otherwise `<short-topic>` can
be a brief paraphrase of the title.

In **cursor stack mode** (4b): the new branch is
`claude/<area>-<topic>`, no `T-NNN` prefix (cursor flow doesn't use
the queue). Derive from the conversation: the user usually names the
next slice when they cue stacking. If they didn't, ask. Examples:

- `claude/render-glow-pulse-tuning` (stacked on
  `claude/render-glow-pulse`)
- `claude/game-pheromone-decay` (stacked on `claude/game-pheromones`)

In **standard mode** (4c): ask the user what the next task is if they
haven't already told you. Derive a short, kebab-case branch name from
the task, prefixed `claude/<area>-`:

- `claude/game-ant-pheromones`
- `claude/engine-velocity-drag-refactor`
- `claude/render-lod-threshold-tuning`

Either way: do not pick a name with a random suffix — permanent worktrees
work best with human-readable, topic-named branches.

### 6. Discard any staged or working-tree changes from the old branch

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

### 7. Check out the new branch off the right base

```bash
git checkout -B <new-branch> origin/master      # standard mode
git checkout -B <new-branch> <old-branch-name>  # fleet-stack or cursor-stack
```

The base is `origin/master` for the standard flow, `<old-branch-name>`
for either stack mode (see step 4). `-B` (uppercase) creates the branch
if it doesn't exist AND resets it to the named commit if it does.
Lowercase `-b` errors out with "branch already exists" — surprisingly
common because the worktree's previous scratch branches accumulate over
many iterations. With `-B`, the branch always lands on the requested
base regardless of its prior state.

In **standard mode**, this starts the new branch from the tip of
`origin/master`. Critical: without `origin/master`, you'd branch off
your old PR branch and carry its commits forward.

In **fleet stack mode** and **cursor stack mode**, this is intentional —
the downstream slice is meant to contain the upstream commits so the
human (or worker) can keep building. The downstream PR's `--base` (set
later by `commit-and-push`) is the upstream branch, so the diff still
shows only the downstream changes.

**macOS sandbox note.** `git checkout -B` writes `.git/config` to set up
branch tracking. On macOS Cursor's Bash sandbox, `.git/config` writes
need the `all` permission — without it the checkout *appears* to
succeed but the tracking config is missing, leaving the new branch
without an upstream. Always run this checkout with sandbox bypass on
macOS.

**Do NOT** `git rebase origin/master` on the old branch and keep working
on it. That would mix old PR commits with new work, which pollutes the
old PR when you push. Always start a new branch.

### 7b. Record cursor-stack-base (cursor stack mode only)

In **cursor stack mode** only, persist the parent branch name as
git config on the new branch so `commit-and-push` can find it later
when opening the PR:

```bash
git config branch.<new-branch>.cursor-stack-base <old-branch-name>
```

This config write is also `.git/config` — same `all` permissions
requirement on macOS as step 7.

The config is per-branch and survives chat boundaries: if the human
opens a new chat tomorrow already on `<new-branch>`, `commit-and-push`
reads the config from `branch.<new-branch>.cursor-stack-base` and
opens the PR with the correct `--base`. No "resume stack" cue is
needed across chats.

Skip this step in fleet stack mode (the chain lives in
`fleet-claim` state, not git config) and in standard mode (no
parent to record).

### 8. Sanity-check the state

```bash
git status
git log --oneline -5
```

- `git status`: should be `nothing to commit, working tree clean`.
- **Standard mode:** `git log --oneline -5` top commit should be the
  latest `master` commit, not one of your previous PR's commits.
- **Fleet/cursor stack mode:** the top commit should be the just-opened
  PR's tip (i.e. the last commit on the old branch). The new branch's
  merge-base with `origin/master` is whatever the old branch branched
  from — not the old branch's tip. That's expected.

In cursor stack mode, also confirm the config write took:

```bash
git config --get branch.<new-branch>.cursor-stack-base
```

It should print `<old-branch-name>`. If it's empty, the write was
sandboxed (see step 7b's macOS note) — re-run with `all` permissions
before continuing.

If the top commit is wrong for the mode you're in, the checkout went
wrong — stop and investigate.

### 9. Read the relevant CLAUDE.md for the new task area

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

### 10. Report

Reply with a compact summary:

- **Mode** you ended up in (standard / fleet-stack / cursor-stack).
- Old branch name + its PR URL (from step 2).
- New branch name + the base it's tracking. In standard mode that's
  fresh `origin/master`; in either stack mode call out the upstream
  branch explicitly so the reviewer-and-merger pipeline (or the human
  in cursor flow) isn't surprised when `commit-and-push` later sets
  `--base <upstream>`.
- In cursor stack mode: confirm `branch.<new>.cursor-stack-base` was
  recorded.
- The CLAUDE.md files you just read to prime context.
- One sentence: "Ready for <next task>. Go ahead."

## Anti-patterns

- ❌ Switching branches with a dirty working tree. Always clean first.
- ❌ Branching off your previous PR branch in **standard mode** (no
  active molecule and no stack cue). That stacks unrelated work and
  pollutes the old PR. Either stack mode (4a returned a `T-NNN`, or
  the human cued stacking) is the only case where the old branch is
  the correct base.
- ❌ Branching off `origin/master` in **either stack mode**. The
  downstream slice's diff would then include the upstream changes
  too, defeating the whole point of stacked PRs (one slice = one
  isolated diff).
- ❌ Skipping `fleet-claim molecule advance` after `commit-and-push`
  and going straight to `start-next-task`. `molecule resume` would
  return the just-completed task as still in-progress, branching you
  onto the same thing you just shipped. The stack-mode sanity-check
  in step 4a catches this — heed it.
- ❌ Writing `cursor-stack-base` git config in fleet stack mode or
  standard mode. The config is the cursor-flow stack signal; setting
  it elsewhere confuses `commit-and-push`.
- ❌ Auto-detecting cursor stack mode from "old branch happens to be
  a feature branch" without a user cue. The cue is the only signal.
  Without it, the human's intent is ambiguous (they may want a fresh
  slice off master), and step 4b's "ask, don't guess" rule applies
  for cursor flow.
- ❌ Running `git rebase origin/master` on the old branch to "catch
  it up", then reusing it. Rebasing the same branch for new unrelated
  work is how PR histories become unreadable.
- ❌ Deleting the old local branch. Leave it alone — if the reviewer
  asks for changes, you'll need to check it out again. Both stack
  modes REQUIRE the old branch as the new branch's base.
- ❌ Starting the next task without reading the target area's
  CLAUDE.md. That's where the module-specific invariants live.
- ❌ Invoking this skill when no PR was actually opened (i.e. after a
  `commit-and-push` failure). Check step 2.

## Recovery

If you discover after switching branches that the old branch had uncommitted
work (somehow step 1's clean check missed it):

1. `git checkout <old-branch>`
2. Stash or commit the dirty work on the old branch.
3. Push and update the PR.
4. `git checkout <new-branch>` to return.

If you set up cursor stack mode and later realize the new slice should
not actually be stacked (e.g. you decide to base it on master after
all):

1. Clear the config: `git config --unset
   branch.<new-branch>.cursor-stack-base` (needs `all` permissions on
   macOS).
2. Rebase the new branch onto master:
   `git rebase --onto origin/master <old-branch> <new-branch>`.
3. Continue work. `commit-and-push` will now open the PR vs `master`.
