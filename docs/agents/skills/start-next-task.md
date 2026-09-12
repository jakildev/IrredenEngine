# start-next-task — shared flow

Reset the current worktree to a fresh feature branch for the next task.

Each repo's `.claude/skills/start-next-task/SKILL.md` is a thin wrapper
that points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)). A step
that needs a repo-specific value names its **delta key** in bold.

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

A repo without stacking or fleet-claim machinery declares those deltas
"n/a"; the corresponding modes never activate.

---

## Modes

Selected in priority order at step 4:

- **Fleet stack mode** — an active `fleet-claim` molecule with remaining
  tasks. The new branch bases on the **old branch** (the just-opened PR's
  head) so the downstream PR's `--base <upstream>` keeps its diff scoped.
- **Cursor stack mode** — no molecule, but the human cued stacking ("stack
  this", "next slice stacked", "keep stacking", "stack the next on this
  PR", "build on the last PR"). Same base; persistence is a git-config key
  (**fleet doc** "Stacking in cursor flow").
- **Standard mode** — fresh **default branch** at the **remote**.

## Preconditions

- The **worktree-assert command** exits 0 (human-only override
  `FLEET_ALLOW_MAIN_CLONE=1`) — a `git checkout -B` in the shared clone
  yanks a branch someone else has checked out.
- Working tree clean. Dirty → stop: the changes belong in the previous PR
  or are WIP the user wants kept.
- No unpushed commits on the current branch (they would be stranded).
- `gh` authenticated.

---

## Flow

### 1. Record the old branch

`git rev-parse --abbrev-ref HEAD` — the report cites it and stack modes
base on it.

### 2. Confirm its PR is open

```bash
gh pr list --head <old-branch-name> --state open --json number,url,title
```

One PR → note it. Zero → stop; the previous `commit-and-push` did not
finish or the PR was closed, and switching would lose un-PR'd work.
Several → note all and continue.

### 2b. Task-boundary handoff

`mkdir -p ~/.fleet/handoff/`, then write
`~/.fleet/handoff/<your-worktree-basename>.md` (overwritten each boundary;
survives `/clear`) and keep an in-context copy. Four buckets: **Shipped**
(PRs, one line each); **In flight / owed**; **Durable decisions / lessons**
(each as a pointer to its durable home — a `docs/design/` doc, the
feedback file, the issue's `## Plan` comment — never inline); **Drop list**
(context now safe to forget). Always emit; scale the size to the session.

### 3. Fetch

`git fetch <remote> <default-branch>` — never branch from a stale local
ref.

### 4. Detect mode (first match wins)

**4a. Fleet stack.** `fleet-claim molecule resume <your-worktree-name>`
always exits 0; read stdout:

- an issue number → active stack; that number is the **next** task (resume
  marks it in-progress). If it equals the old branch's issue prefix, the
  molecule was not advanced after `commit-and-push` — stop and have the
  user run `fleet-claim molecule advance <agent> <id> done pr=<URL>
  commit=<SHA>` first. Otherwise `MODE=fleet-stack`, `BASE=<old-branch>`.
- empty → no molecule; continue.

A non-zero exit is a real fault — surface stderr.

**4b. Cursor stack.** A stacking cue → `MODE=cursor-stack`,
`BASE=<old-branch>`. A fresh-start cue ("next task", "I merged it", "back
to master") → 4c. An ambiguous cue when the old branch already has
`cursor-stack-base` set → ask; don't guess.

**4c. Standard.** `MODE=standard`, `BASE=<remote>/<default-branch>`.

### 5. Branch name

- Fleet stack: `<branch prefix><issue#>-<short-topic>`; topic from the
  issue title (`fleet-issue view <issue#>`, which falls back to the scout
  cache when `gh` is unreachable).
- Cursor stack: `<branch prefix><area>-<topic>` from the conversation; ask
  if the user has not named the slice.
- Standard: ask what the next task is if unsaid; `<branch
  prefix><area>-<topic>` using the **area examples**.

Human-readable topic names, no random suffixes.

### 6. Discard leftovers

```bash
git restore --staged .
git checkout -- .
```

### 7. Check out the new branch

```bash
git checkout -B <new-branch> <remote>/<default-branch>   # standard mode
git checkout -B <new-branch> <old-branch-name>           # fleet/cursor stack
```

`-B` resets an existing scratch branch of the same name. Never `git rebase
<remote>/<default-branch>` on the old branch and keep working there — that
pollutes the old PR on the next push.

`git checkout -B` writes `.git/config`; on a sandboxed Bash (macOS Cursor)
run it with the elevated permission or the tracking config is silently
missing.

### 7b. Cursor stack only

```bash
git config branch.<new-branch>.cursor-stack-base <old-branch-name>
```

Same `.git/config` write as step 7.

### 8. Sanity check

`git status` clean; `git log --oneline -5` shows the default-branch tip
(standard) or the just-opened PR's tip (stack modes). In cursor stack mode
`git config --get branch.<new-branch>.cursor-stack-base` prints the old
branch — empty means retry 7b with elevated permission. Wrong tip → stop
and investigate.

### 9. Prime context

Read the most specific `CLAUDE.md` for the directory the next task touches
(a creation's own `CLAUDE.md` overrides the engine baseline there).

### 10. Report

Mode; old branch + PR URL; new branch + its base (name the upstream branch
explicitly in stack modes, so the later `--base <upstream>` is expected);
in cursor stack mode, that `cursor-stack-base` was recorded; the
`CLAUDE.md` files read; "Ready for <next task>."

---

## Recovery

- Old branch had uncommitted work: `git checkout <old-branch>`, commit it,
  push, update the PR, `git checkout <new-branch>`.
- Cursor stack slice should base on the default branch after all: `git
  config --unset branch.<new-branch>.cursor-stack-base` (elevated
  permission where applicable), then `git rebase --onto
  <remote>/<default-branch> <old-branch> <new-branch>`.

Leave the old local branch in place — a review fix needs it, and both
stack modes base on it.
