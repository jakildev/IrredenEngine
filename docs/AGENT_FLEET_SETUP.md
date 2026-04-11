# Parallel-agent workflow: external setup checklist

This is the list of things **you** (the user) need to do once, outside
the repo, to run the parallel-agent workflow described in the top-level
`CLAUDE.md`. Nothing in the repo can set these up for you — they're
environment, auth, and process.

Work through the list top-to-bottom. Each step is independent unless
noted.

---

## 1. GitHub CLI install + auth

Required for the `commit-and-push` and `review-pr` skills. **As of
2026-04-11, `gh` is not installed on this machine** — install it first:

```powershell
# from an elevated PowerShell or Windows Terminal:
winget install --id GitHub.cli
```

Then open a new shell (to pick up the updated `PATH`) and verify:

```bash
gh --version
gh auth status
```

If not logged in:

```bash
gh auth login
```

Scopes you need: `repo`, `read:org`, `workflow`. Verify with:

```bash
gh auth status | grep "Token scopes"
```

Until `gh` is installed and authed, `commit-and-push` can still commit +
push the branch, but the PR has to be opened manually in the GitHub web
UI (the skill will tell you and give you the branch URL).

---

## 2. Permanent worktrees for the agent fleet

The existing `.claude/worktrees/` are ephemeral, created ad-hoc. For a
permanent fleet, create named worktrees once and reuse them across
sessions.

Decide how many you want running in parallel. A reasonable starting
setup with the model split in mind:

| Worktree              | Model  | Role                                |
|-----------------------|--------|-------------------------------------|
| `opus-architect`      | Opus   | Core engine work, ECS/render/audio  |
| `sonnet-fleet-1`      | Sonnet | TASKS.md items, tests, docs         |
| `sonnet-fleet-2`      | Sonnet | TASKS.md items, tests, docs         |
| `sonnet-reviewer`     | Sonnet | First-pass PR review                |
| `opus-reviewer`       | Opus   | Second-pass review + merge sign-off |

Create them once:

```bash
cd C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine

git worktree add .claude/worktrees/opus-architect master
git worktree add .claude/worktrees/sonnet-fleet-1 master
git worktree add .claude/worktrees/sonnet-fleet-2 master
git worktree add .claude/worktrees/sonnet-reviewer master
git worktree add .claude/worktrees/opus-reviewer master
```

These live forever. Each agent session opens the worktree it wants and
the `start-next-task` skill resets its branch back to `origin/master`
after each PR, so the worktree itself is reused but the branch name
changes each task.

You can delete the old ephemeral worktrees (`adoring-gates`,
`goofy-jones`, `vectorized-sauteeing-yeti`) once this PR merges — they
were from the bootstrap phase and have no persistent role.

---

## 3. Model defaults per worktree

In Claude Code, each pane/session can run on a different model. When
you start a session against a worktree, set the model to match the
role in the table above. Inside a running session you can still switch
with `/model`, but starting on the right model avoids burning Opus
budget on routine work.

Practical rule:

- **Opus budget is precious.** Reserve it for the worktrees named
  `opus-*` and for cases where a Sonnet session hits something subtle
  (escalate with `/model opus`).
- **Sonnet is the fleet.** Let sonnet-named worktrees run unattended
  against `TASKS.md` and first-pass reviews. Docs passes, test
  generation, mechanical refactors all go here.

The top-level `CLAUDE.md` "Model split" section has the full rules and
`TASKS.md` uses a `**Model:**` tag on each task.

---

## 4. `settings.json` — permissions and allowlist (optional but helpful)

If you want Sonnet-fleet agents to run unattended overnight, loosen the
default confirmation prompts for commands they need. Edit your Claude
Code settings (not the repo) and add an allowlist for build / test /
`gh` / read-only shell commands. Keep **writes to master, force pushes,
and destructive git commands** on the confirm list.

Rough starting allowlist:

- `cmake --build ...`
- `cmake --preset ...`
- `gh pr view`, `gh pr diff`, `gh pr list`, `gh pr review --comment`,
  `gh pr create` (but *not* `gh pr merge`)
- `git status`, `git diff`, `git log`, `git fetch`, `git checkout -b`,
  `git add`, `git commit`, `git push -u origin HEAD`
- `cmd.exe /c "set PATH=... && <build command>"` — the PATH-fix wrapper

Leave on prompt:

- `git push origin master`
- `git push --force`
- `git reset --hard`
- `gh pr merge`
- Anything destructive to `.claude/worktrees/`

---

## 5. `creations/game/` and private repos — decide ownership

Your `creations/game/` (and any other private creation) is gitignored
in the engine repo. You have three options:

1. **Keep private creations fully local.** Agents that want to work on
   them must do so from the main worktree only, because the git
   worktrees won't have the directory. Simplest but limits parallelism
   inside a private creation.
2. **Make each private creation its own git repo.** Nest a separate
   `.git` inside `creations/game/` (or use a submodule). Then the
   private creation has its own worktrees, its own TASKS.md, its own
   `commit-and-push` and `review-pr` skills, its own PR workflow. This
   is the scalable option.
3. **Un-gitignore** `creations/game/` in the engine repo. Not
   recommended — mixes public engine and private game history in one
   repo.

Recommended: **option 2**, with the private repo following the same
parallel-agent pattern (its own `CLAUDE.md`, `TASKS.md`, skills). The
engine-level skills in `.claude/skills/` are already written generically
so they apply inside a private creation's subtree too, but a private
creation can override any of them by shipping its own `.claude/skills/`
under its own directory.

---

## 6. Reviewer-worktree workflow

The reviewer agents (`sonnet-reviewer`, `opus-reviewer`) need to
frequently `gh pr checkout <N>`. That requires the reviewer worktree to
be on a throwaway branch so `gh pr checkout` can replace it without
protesting.

Before starting a reviewer session:

```bash
cd .claude/worktrees/sonnet-reviewer
git checkout -B review-scratch origin/master
```

Then inside the session you can `gh pr checkout 42`, review, post the
comment, and `git checkout -B review-scratch origin/master` again for
the next PR.

Don't commit or push from a reviewer worktree. The `review-pr` skill
enforces this, but a human reviewer should know too.

---

## 7. Build sanity once per boot

Before you start an agent on engine work, prove the build works from
the worktree you'll use. Open one Bash tool and run the canonical
build command from `CLAUDE.md`. If it fails, debug it yourself — don't
let the agent waste cycles on a pre-existing build break.

```bash
cmd.exe /c "set PATH=C:\\msys64\\mingw64\\bin;%PATH% && \"C:/Program Files/CMake/bin/cmake.EXE\" --build C:/Users/evinj/VSCODE_PROJECTS/repos/IrredenEngine/build --target IRShapeDebug -- -j4" 2>&1
```

Remember: the build tree is still configured against the main worktree,
so C++/GLSL/Lua edits inside a worktree must be mirrored to the main
tree for the build to see them. See `CLAUDE.md` "Build" section.

---

## 8. Session starter prompts

When kicking off a worktree session, start with a one-liner that tells
the agent which role it's in. Examples:

- **Opus architect session:**
  > "You are the Opus architect agent in `.claude/worktrees/opus-architect`. Pick the next unblocked `[opus]` task from `TASKS.md`, complete it, and open a PR via the `commit-and-push` skill. Build the target you touched and run the relevant executable before declaring done."

- **Sonnet fleet session:**
  > "You are a Sonnet fleet agent in `.claude/worktrees/sonnet-fleet-1`. Pick the next unblocked `[sonnet]` task from `TASKS.md`, complete it, and open a PR via the `commit-and-push` skill. If the task turns out to touch core engine invariants (rendering, ECS, ownership, concurrency), stop and requeue it as `[opus]` with a note instead of charging ahead. Build and run before declaring done."

- **Sonnet reviewer session:**
  > "You are a Sonnet first-pass reviewer in `.claude/worktrees/sonnet-reviewer`. Review the oldest unreviewed open PR using the `review-pr` skill. Post a first-pass review and end with an explicit escalation line saying whether Opus recheck is required."

- **Opus reviewer session:**
  > "You are the Opus final reviewer in `.claude/worktrees/opus-reviewer`. Review any open PR that has a Sonnet first-pass review flagged for Opus escalation, or any open PR touching core engine invariants. Post a final review and, if approving, use `gh pr review <N> --approve`. Do not merge — I merge."

Save these as Claude Code custom commands (or just paste them) so you
don't retype them each morning.

---

## 9. What *not* to automate

A few things should stay manual even with a fleet running:

- **Merging PRs.** You merge. Agents never call `gh pr merge`.
- **Pushing to `master`.** Agents never `git push origin master`.
- **Force-pushing anywhere.** Agents never `--force`.
- **Deleting worktrees.** Agents never touch `.claude/worktrees/`
  layout.
- **Configuring the build.** Agents never run `cmake --preset`.
- **Editing this document.** (Or they can, but you sign off.)

The `commit-and-push` and `review-pr` skills enforce most of this, but
a human check is the last line of defense.

---

## 10. Dry run

Before handing work to the fleet, do one manual end-to-end run with
yourself in the loop:

1. Open a session against `opus-architect`.
2. Ask it to pick the example `TASKS.md` item (`benchmark IRShapeDebug
   at zoom 4`) and work through it.
3. Watch the `commit-and-push` skill open a PR.
4. Open a second session against `sonnet-reviewer`, have it review.
5. Open a third session against `opus-reviewer`, have it re-review.
6. Merge manually.
7. Back in `opus-architect`, run `start-next-task`, verify the branch
   resets cleanly.

If any of those steps fail, fix the skill — not the task. The point of
the dry run is to uncover workflow bugs, not to complete a task.

---

## Recap

- GitHub CLI authed. ✅
- Permanent worktrees created. ✅
- Model defaults per worktree memorized. ✅
- Optional settings.json allowlist. ✅
- Decision made about `creations/game/` (or other private creations). ✅
- Reviewer worktrees parked on `review-scratch`. ✅
- Build sanity verified. ✅
- Session starter prompts saved. ✅
- "What not to automate" understood. ✅
- One dry run through the full loop completed. ✅

Then let the fleet loose.
