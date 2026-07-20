# start-next-task — shared flow

The canonical `start-next-task` flow. Every repo that runs a fleet keeps
its `.claude/skills/start-next-task/SKILL.md` as a thin wrapper that points
here and supplies only its repo-specific **deltas** (below), so the flow
itself is single-sourced and the wrappers cannot drift. See
[`docs/design/skill-sharing.md`](../../design/skill-sharing.md) for the
mechanism.

Phrased in repo-neutral terms: wherever a step needs a repo-specific value
it names a **delta key** in bold and the invoking wrapper resolves it from
its own `## Deltas` section.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value (example) |
|---|---|---|
| **default branch** | The branch fresh work bases off in standard mode. | `master` |
| **remote** | The git remote PRs target. | `origin` |
| **branch prefix** | Prefix for new feature branches. | `claude/` |
| **worktree-assert command** | Command that fails if you're in the shared main clone. | `fleet-assert-worktree` |
| **fleet doc** | The repo's fleet reference for the stacking-persistence mechanism. | [`docs/agents/FLEET.md`](../FLEET.md) |
| **area examples** | Example `<area>` tokens for branch names. | `engine`, `render`, `game` |

A creation that does not stack, or has no fleet-claim machinery, simply
declares those deltas as "n/a" in its wrapper; the corresponding modes
below then never activate.

---

## Modes

The skill operates in three modes, selected in priority order at step 4:

- **Fleet stack mode** (active `fleet-claim` molecule with remaining
  tasks): branch off the **old branch** — the head ref of the PR that
  `commit-and-push` just opened — so the downstream task's branch contains
  the upstream commits while its PR's `--base <upstream>` keeps the diff
  scoped. Selected via `fleet-claim molecule resume`.
- **Cursor stack mode** (cursor flow only, driven by user cue): same
  mechanic (branch off the old branch) but no molecule machinery. Activated
  by stacking cues ("stack this", "next slice stacked", "keep stacking",
  "stack the next on this PR") when no fleet molecule is active; the
  **fleet doc** "Stacking in cursor flow" section documents the git-config
  persistence.
- **Standard mode** (no fleet molecule, no stack cue): branch off fresh
  **default branch** at the **remote**. The historical behavior and most
  common case.

---

## Preconditions

1. **You must be in your own worktree, not the shared main clone.** Run the
   **worktree-assert command** (optionally with your worktree basename). If
   it exits non-zero, STOP and `cd` into your worktree before resetting the
   branch — a `git checkout -B` in the shared main clone yanks the branch
   another agent or the operator has checked out there. Human-only override:
   `FLEET_ALLOW_MAIN_CLONE=1`.
2. The working tree should be **clean**. If `git status` shows uncommitted
   changes, stop and warn — either they belong in the previous PR (go back
   to `commit-and-push`) or they are a WIP the user wants to keep.
3. You should be on a feature branch whose PR is **already open and
   pushed**. If the current branch has unpushed commits, stop and warn —
   those commits would be stranded by the branch switch.
4. `gh` authenticated.

---

## Flow

### 1. Record what branch you were on

```bash
git rev-parse --abbrev-ref HEAD
```

Remember this — you reference it in the report, the user may want to check
the PR is still tracking it, and stack mode (step 4) uses it as the new
branch's base.

### 2. Confirm the old branch's PR is pushed and open

```bash
gh pr list --head <old-branch-name> --state open --json number,url,title
```

- **One PR** → good; note the number and URL for the report.
- **Zero PRs** → stop and warn. Either the previous `commit-and-push`
  didn't finish or the PR was closed. Do not switch branches until the user
  says what to do — un-PR'd work would be lost.
- **Multiple PRs** → unusual. Note all of them and continue.

### 2b. Task-boundary closeout (before the reset)

Before switching branches, distill the task you just finished into a compact,
high-signal handoff — so the next task starts lean but not amnesiac. Run
`mkdir -p ~/.fleet/handoff/` first (other fleet steps create `~/.fleet/`
subdirs explicitly; do the same here). Write the handoff to
`~/.fleet/handoff/<your-worktree-basename>.md` (e.g. `pool-2.md`;
per-worktree, overwritten each boundary;
`~/.fleet/` survives a `/clear` on this host — and cross-host too if your
`~/.fleet/` is synced) AND keep a tight in-context copy. Four buckets:

- **Shipped** — the PR(s) opened/merged this task, one-line outcome each.
- **In flight / owed** — open PRs, follow-up issues filed, anything blocked-on.
- **Durable decisions / lessons** — anything that should outlive the task, each
  with a **pointer** to its durable home (a `docs/design/` doc, the feedback
  file, or `~/.fleet/plans/issue-<N>.md`) — never duplicated inline.
- **Drop list** — the prior context now safe to forget (tool dumps, resolved
  escalations, superseded drafts).

This **complements** the harness's automatic summarization, it does not replace
it: the closeout is the agent-authored distillation the harness can't produce (it
can't tell durable from droppable) and a re-hydration source the harness summary
is not (re-read on the next task / after a `/clear` — see the role startup's
"read your handoff file" step). Keep it lightweight by default and scale depth
with context size — a long multi-task session warrants a fuller closeout, a quick
two-task hop a one-liner. Always emit; scale the size, never skip.

### 3. Fetch latest remote default branch

```bash
git fetch <remote> <default-branch>
```

Never rely on a stale local default-branch ref. Always fetch before
branching.

### 4. Detect mode

Three modes, first match wins.

#### 4a. Fleet stack mode

If the current task is part of a `fleet-claim stack` chain, the next branch
must build on the just-opened PR's head ref. Probe the molecule (agents
know their own worktree name from their role instructions):

```bash
fleet-claim molecule resume <your-worktree-name>
```

Interpret stdout (the helper always exits 0; discriminate via stdout):

- **stdout names an issue number** — active stack with remaining tasks. The
  number is the **next** task (resume marks the first pending task
  in-progress as a side effect).
  - **Sanity-check:** if the returned number matches the old branch's issue
    prefix, the worker forgot to run `fleet-claim molecule advance <agent>
    <id> done pr=<URL> commit=<SHA>` after `commit-and-push`. Stop and tell
    the user to advance the molecule first — else you branch back onto the
    same task.
  - Otherwise the new branch is `<branch prefix><returned-issue#>-<short-topic>`
    based on the **old branch**. Set `MODE=fleet-stack`,
    `BASE=<old-branch-name>`, remember `<returned-issue#>` for step 5.
- **stdout is empty** — no fleet molecule. Continue to 4b.

If `molecule resume` exits non-zero, stop and surface stderr — that's a
real fault, not a "no work" signal. In a cursor-flow session with no
fleet-claim machinery this probe returns empty (expected).

#### 4b. Cursor stack mode

If 4a returned empty AND the cue contains a stacking phrase ("stack this",
"next slice stacked", "keep stacking", "stack the next on this PR", "build
on the last PR"): set `MODE=cursor-stack`, `BASE=<old-branch-name>`.
Step 7b persists the parent branch.

If the cue is fresh-start ("next task", "I merged it", "back to master",
etc.): continue to 4c.

If the cue is ambiguous and the old branch already has `cursor-stack-base`
set, ask whether to continue the stack or branch fresh — don't guess (see
the **fleet doc** "Stacking in cursor flow").

#### 4c. Standard mode

Default. `MODE=standard`, `BASE=<remote>/<default-branch>`. Proceed.

### 5. Derive a new branch name

- **Fleet stack mode:** `<branch prefix><issue#>-<short-topic>`. Pull the
  topic from the issue title (`fleet-issue view <issue#>`, falling back to
  `gh issue view <issue#>`). Prefer a name the user already gave.
- **Cursor stack mode:** `<branch prefix><area>-<topic>`, no issue-number
  prefix. Derive from the conversation; the user usually names the next
  slice when cueing the stack. If not, ask. Example: a tuning slice stacked
  on a feature branch.
- **Standard mode:** ask the user what the next task is if they haven't
  said. Derive a short kebab-case name prefixed `<branch prefix><area>-`,
  using one of the repo's **area examples**.

Either way: no random suffixes — permanent worktrees work best with
human-readable, topic-named branches.

### 6. Discard any staged or working-tree changes from the old branch

```bash
git restore --staged .
git checkout -- .
```

No-ops if the precondition clean-check passed; otherwise they clear
leftovers that would fail the next `git checkout` with "your local changes
would be overwritten by checkout."

### 7. Check out the new branch off the right base

```bash
git checkout -B <new-branch> <remote>/<default-branch>   # standard mode
git checkout -B <new-branch> <old-branch-name>           # fleet/cursor stack
```

`-B` (uppercase) creates the branch if absent AND resets it to the named
commit if present — lowercase `-b` errors "branch already exists", common
because a worktree accumulates scratch branches over many iterations.

In **standard mode** this starts from the tip of the remote default branch;
without it you'd carry the old PR's commits forward. In **stack modes** the
downstream slice intentionally contains the upstream commits — the
downstream PR's `--base` (set later by `commit-and-push`) is the upstream
branch, so the diff still shows only the downstream changes.

**Sandbox note.** `git checkout -B` writes `.git/config` for branch
tracking. On a sandboxed Bash (e.g. macOS Cursor), `.git/config` writes
need the elevated permission — without it the checkout *appears* to succeed
but the tracking config is missing. Run this checkout with sandbox bypass
where that applies.

**Do NOT** `git rebase <remote>/<default-branch>` on the old branch and
keep working on it — that mixes old PR commits with new work and pollutes
the old PR when you push. Always start a new branch.

### 7b. Record the cursor-stack base (cursor stack mode only)

```bash
git config branch.<new-branch>.cursor-stack-base <old-branch-name>
```

Same `.git/config` write as step 7 — needs the elevated sandbox
permission where applicable. Skip in fleet stack and standard modes. See
the **fleet doc** "Stacking in cursor flow" for the cross-chat persistence.

### 8. Sanity-check the state

```bash
git status
git log --oneline -5
```

- `git status`: `nothing to commit, working tree clean`.
- **Standard mode:** top commit is the latest default-branch commit, not
  one of your previous PR's commits.
- **Stack modes:** top commit is the just-opened PR's tip. The new branch's
  merge-base with the remote default branch is whatever the old branch
  branched from — expected.

In cursor stack mode also run
`git config --get branch.<new-branch>.cursor-stack-base` — should print the
old branch; if empty, retry step 7b with elevated permissions.

If the top commit is wrong for the mode, the checkout went wrong — stop and
investigate.

### 9. Read the relevant CLAUDE.md for the new task area

Before starting, read the most specific `CLAUDE.md` for the directory
you're about to work in (e.g. a module's own `CLAUDE.md`, or a creation
subdirectory's `CLAUDE.md` — creations define their own conventions). This
primes your context with the module's gotchas. If the subdirectory has a
dedicated workflow that differs from the repo baseline, honor it.

### 10. Report

Reply with a compact summary:

- **Mode** (standard / fleet-stack / cursor-stack).
- Old branch name + its PR URL (from step 2).
- New branch name + the base it tracks. In standard mode that's the fresh
  remote default branch; in either stack mode call out the upstream branch
  explicitly so the pipeline isn't surprised when `commit-and-push` later
  sets `--base <upstream>`.
- In cursor stack mode: confirm `branch.<new>.cursor-stack-base` was
  recorded.
- The CLAUDE.md files you read to prime context.
- One sentence: "Ready for <next task>. Go ahead."

---

## Anti-patterns

- Running `git rebase <remote>/<default-branch>` on the old branch to
  "catch it up", then reusing it. Rebasing the same branch for unrelated
  new work is how PR histories become unreadable.
- Deleting the old local branch. Leave it — if the reviewer asks for
  changes you'll need it again, and both stack modes REQUIRE it as the new
  branch's base.

## Recovery

If you discover after switching that the old branch had uncommitted work:

1. `git checkout <old-branch>`
2. Stash or commit the dirty work on the old branch.
3. Push and update the PR.
4. `git checkout <new-branch>` to return.

If you set up cursor stack mode and later decide the slice should base on
the default branch after all:

1. `git config --unset branch.<new-branch>.cursor-stack-base` (elevated
   permission where applicable).
2. `git rebase --onto <remote>/<default-branch> <old-branch> <new-branch>`.
3. Continue. `commit-and-push` will now open the PR vs the default branch.
