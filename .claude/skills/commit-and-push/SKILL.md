---
name: commit-and-push
description: >-
  Stage, commit, push a feature branch, and open a GitHub PR against master for
  the Irreden Engine repo. Use whenever the user says "commit", "commit my
  changes", "commit and push", "open a PR", "make a PR", "wrap up this chunk",
  or otherwise indicates the current slice of work is ready for review. The
  skill assumes the parallel-agent workflow where work happens on short-lived
  feature branches, another agent reviews the PR, and the user merges via the
  GitHub UI. NEVER commit directly to master.
---

# commit-and-push

End-to-end flow for packaging a chunk of work into a reviewable PR on the
Irreden Engine repo. This repo runs a parallel-agent workflow: each working
agent lives in its own worktree, commits on a short-lived feature branch, and
opens a PR that a reviewer agent inspects before the user merges.

## When to invoke

Trigger this skill whenever the user says something like:

- "commit" / "commit my changes" / "commit and push"
- "open a PR" / "make a PR" / "send it for review"
- "wrap up" / "save progress" / "this slice is done"

Do **not** invoke proactively — only when the user explicitly asks.

## Preconditions — do these before anything else

1. **You must NOT be on `master`.** If `git rev-parse --abbrev-ref HEAD` reports
   `master`, stop and warn the user. Branch off master first (see step 2 of
   the flow) — do not commit to master. The old direct-push workflow is
   retired.
2. **`gh` must be authenticated.** If `gh auth status` fails, stop and ask the
   user to run `gh auth login`.
3. **The working tree must have something to commit.** If `git status` is
   clean, tell the user and stop.

## Stack-aware mode

This is the **fleet stack** path. The cursor-flow stacking variant
lives in the next section ("Cursor stack mode"); they have separate
detection signals and are mutually exclusive in practice.

If the current task is part of an `fleet-claim stack` chain (i.e. the
caller's worktree name has a stack claim under
`~/.fleet/claims/_stack_<agent>/`), this skill opens **one PR per task,
chained by `--base`** instead of a single PR with multiple commits.

Detect stack mode at the start of the flow:

```bash
fleet-claim stack-pr-state <your-worktree-name>
```

- Output `no stack claim for agent: <name>` → not stacked, proceed with
  the normal single-PR flow below.
- Output with `task`/`branch`/`pr` columns → stacked. The row whose PR
  column is `(pending)` and whose earlier rows (if any) are all filled
  is the current task. Note its `<task-id>`; you will need it in steps
  2 and 8.

The deltas versus the single-PR flow:

- **Step 2 branch name** is `claude/<task-id>-<short-topic>` (e.g.
  `claude/T-005-occupancy-grid`). The task ID prefix lets the
  queue-manager, reviewers, and `stack-base` resolve the chain.
- **Step 8 PR base** is `fleet-claim stack-base <agent> <task-id>`
  instead of `master`. For the first task this still returns `master`;
  for subsequent tasks it returns the previous task's branch.
- **After `gh pr create`** record the PR in the stack so the next task
  can chain off it:
  ```bash
  fleet-claim stack-set-pr <agent> <task-id> <branch> <pr-url>
  ```
- **PR body** includes a `## Stack context` block with a `Stacked on:`
  line (the previous PR URL, or `master` for the first task in the
  chain) and a `Full chain:` line listing the task IDs the molecule
  covers. Reviewers use this to navigate sibling PRs without leaving
  the diff.
- **Labels** include `fleet:stacked` whenever `--base != master` (i.e.
  every PR in the chain except the first). The merger reads
  `baseRefName` directly for routing decisions; the label is a derived
  convenience for human visibility and cheap GitHub-side filtering.
- **Title** starts with `T-NNN: ` so the queue-manager's per-task
  matching works and reviewers can tell which task in the chain this
  PR belongs to.

After the PR opens, do NOT start the next task in the same branch.
Invoke `start-next-task` the same way as single-PR work; when it comes
back, the next stacked-PR iteration computes its own `--base` via
`stack-base` and branches off that (not `origin/master`).

When **the final task's PR is merged**, run
`fleet-claim release-stack <agent>` to clean up both the per-task
claims and the stack metadata.

## Cursor stack mode

This is the cursor-flow analog of the fleet stack mode above. It
opens a single PR per slice but with `--base <previous-feature-
branch>` instead of `--base master`, so the diffs stay isolated
while the chain accumulates. No `fleet-claim` machinery, no task
IDs, no `fleet:stacked` label. State lives entirely in the per-
branch git config that `start-next-task` writes when the human
cues stacking.

Detect cursor stack mode after step 1 of the main flow, AFTER
ruling out fleet stack mode:

```bash
git config --get branch.$(git branch --show-current).cursor-stack-base
```

- Output is empty / exit 1 → not cursor-stacked. Proceed with the
  normal single-PR flow.
- Output names a branch (e.g. `claude/render-glow-pulse`) → the
  current branch is cursor-stacked on that branch. Note the value;
  step 8 uses it as `--base` and writes a `Stacked on:` line to
  the PR body.

The deltas vs the normal single-PR flow:

- **Step 8 PR base** is the recorded `cursor-stack-base` instead
  of `master`. Pass it to `gh pr create` as `--base <base>`.
- **PR body** includes a `Stacked on: <PR URL>` line. Look up the
  parent PR URL once at PR-open time:
  ```bash
  parent_branch=$(git config --get branch.$(git branch --show-current).cursor-stack-base)
  parent_pr_url=$(gh pr list --head "$parent_branch" --state all --json url -q '.[0].url' --limit 1)
  ```
  If the parent has no PR yet (e.g. the human hasn't run
  `commit-and-push` on it — unusual but possible), use the branch
  name instead: `Stacked on: <parent_branch>` and warn the user.
- **Title** uses the normal cursor-flow shape (no `T-NNN:` prefix —
  cursor flow doesn't use the queue).
- **No labels** beyond what the normal flow adds. The
  `fleet:stacked` label is fleet-only; cursor flow just relies on
  `Stacked on:` in the PR body.

After the PR opens, do NOT clear the `cursor-stack-base` config —
leave it as a record of the chain. The config is local-only and
doesn't need cleanup; the next `commit-and-push` on a
non-stacked branch (no config set) takes the standard path
automatically.

When the parent PR merges, change this PR's base to `master` in the
GitHub UI (or `gh pr edit <N> --base master`) — same step as in any
manual stacked-PR workflow.

**macOS sandbox note.** Cursor's Bash sandbox blocks `gh` keychain
access and SSH `git push`. Always run `gh pr create`,
`gh pr edit`, `gh pr list`, and `git push` with the `all`
permission on macOS. Reads of `git config --get …` are not
sandboxed.

## Flow

### 1. Gather state (parallel)

Run these in a single Bash-tool batch (one message, multiple tool calls):

```
git rev-parse --abbrev-ref HEAD
git status
git diff --stat && git diff
git log --oneline -10
```

- The branch name tells you whether you're already on a feature branch or
  still on master.
- `git status` / `git diff` — understand **what** changed and **why** so you
  can write the PR body.
- `git log --oneline -10` — match the tone of recent titles.

### 2. Ensure you're on a feature branch

If you're on `master`:

- Derive a short, kebab-case branch name from the session's work. Examples:
  `claude/render-iso-culling-fix`, `claude/game-ant-pheromone-trails`,
  `claude/engine-entity-batch-cleanup`. Prefer prefix `claude/<area>-<topic>`.
- Create the branch **before** staging so the commits land there:
  ```bash
  git checkout -b claude/<area>-<topic>
  ```
- **In stack-aware mode** (see section above): use
  `claude/<task-id>-<short-topic>` instead, and branch off the base
  returned by `fleet-claim stack-base <agent> <task-id>` (which is
  `master` for the first task in the chain, or the previous task's
  branch for subsequent tasks):
  ```bash
  base=$(fleet-claim stack-base <agent> <task-id>)
  git fetch origin "$base"
  git checkout -b claude/<task-id>-<short-topic> "origin/$base"
  ```

If you're already on a feature branch, just use it. Do not rename mid-session.

### 3. Pre-commit checks and `simplify`

**First**, run the **Rebase guard** check (see section below): if you
saved a pre-capture earlier in this conversation (a `git diff
origin/master` snapshot before rebasing), compare it now against a
fresh `git diff origin/master`. If you don't have that snapshot in
context, check `git reflog --since=2.hours.ago` for a recent rebase
entry; if found, warn and inspect manually before proceeding.

**Second**, check whether the diff touches visual/render files:

```bash
git diff --name-only origin/master...HEAD
```

If the diff includes any file under `engine/render/`,
`engine/prefabs/irreden/render/`, any `*.glsl` or `*.metal` file,
`creations/demos/*/src/**`, or any `creations/demos/*/main*.cpp` —
and `docs/pr-screenshots/<current-branch>/` does **not** yet exist —
stop and prompt the worker to run the `attach-screenshots` skill
before proceeding. Screenshots must ship in the
same commit as the code change; a separate follow-up commit for screenshots
is harder to review and misses the "before" baseline.

Skip the prompt if:
- The diff is purely docs, tests, mechanical refactors, or build/CI changes.
- `docs/pr-screenshots/<branch>/` already contains screenshots from a prior
  run on this branch.

**Then**, run the cross-repo information-isolation check (see engine
`CLAUDE.md` "Cross-repo information isolation"). The engine repo is
public; the game repo is private. Engine PRs MUST NOT leak game
content. Scan the staged diff and the to-be-written PR body for any
of the following game-leakage tokens:

- `creations/game/` — engine PRs should never touch game files. If
  the diff includes a path under `creations/game/`, this almost
  certainly means the worker is in the wrong worktree (the engine
  PR shouldn't be modifying the game tree). Stop and warn.
- `jakildev/irreden` — the game repo slug. If a draft PR body or
  commit message references it, scrub before continuing.
- A `T-NNN` task ID that came from the game queue (cross-check
  against `~/src/IrredenEngine/creations/game/TASKS.md` if present).
  Engine PR bodies should reference engine task IDs only.
- Game-specific feature names or design language. This one needs
  human judgment — if the commit message you've drafted talks about
  "ant pheromone trails" or "fungus sporulation" instead of the
  underlying engine capability, rewrite in pure engine terms.

Check for staged game-repo paths with a single git command using a
pathspec filter — no pipe, no compound operator:

```bash
git diff --cached --name-only -- 'creations/game/'
```

If the output is non-empty, the commit includes `creations/game/`
paths and you must stop and warn the user. (If the output is empty,
git prints nothing and you proceed.)

For the PR body / commit message scan (looking for `jakildev/irreden`
references or game task IDs), use the **Grep tool** on the draft text
rather than a shell pipe.

If any leakage is found, surface it to the user before committing.
The fix is usually a 30-second rewrite of the commit message and PR
body in engine-level terms; rarely is a code change actually needed
(if it is, the worker is in the wrong repo entirely).

**Then** invoke the `simplify` skill to review the dirty changes for reuse,
quality, and efficiency issues specific to Irreden Engine:

```
Skill: simplify
```

`simplify` reads the same diff you gathered and looks for things like
per-entity `getComponent` inside system ticks, duplicated helpers, naming-
convention slips, and other project-specific smells. It applies fixes
directly.

**Always run simplify, regardless of file types.** No "doc-only"
or "small change" carve-out. The skill has a doc-side section
(stale cross-references, change-narration prose, drifted examples,
section drift) that earns its keep on markdown-heavy diffs too —
exactly the failure mode that produced the wrong macOS smoke
label on PR #319 (a stale cross-reference between the role doc
and the actual host the author was on).

After `simplify` finishes:

- Re-run `git status` / `git diff --stat` to see the post-simplify state.
- If `simplify` reported findings it **couldn't** fix automatically, surface
  them to the user before committing and ask whether to proceed.
- If the working tree is now clean, stop and report — nothing to commit.

### 4. Draft the commit message

Shape:

```
<area>: <one-line overview of what this slice accomplished>

<1–3 short paragraphs describing the *why* and the non-obvious *what*.
Group related edits. Mention each major subsystem touched. Skip things
obvious from filenames.>

<If the session unearthed build/runtime gotchas worth remembering, note
them here so the commit doubles as a changelog entry.>

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
```

If the work is entirely inside a subdirectory with its own `CLAUDE.md`
that specifies a different commit-message shape (e.g. a private creation
with its own conventions), prefer the subdirectory's shape over this
default. Read the nearest `CLAUDE.md` before drafting.

**Rules:**

- Title ≤ 72 chars, lowercase, no trailing period.
- Prefer a scope prefix (`render:`, `engine/voxel:`, `game/nav:`, `build:`,
  `docs:`) derived from the dominant changed path.
- Body wraps at ~72 chars per line.
- Describe *why* over *what*.
- Always include the `Co-Authored-By` trailer exactly as shown.

### 5. Stage files

Add specific paths. **Never** use `git add -A` or `git add .`:

```
git add <path1> <path2> ...
```

**Exclusions — never stage these unless the user explicitly asks:**

- `.claude/worktrees/` and `.claude/settings.local.json` — worktree/session state.
- `.vscode/*` unless the user asked for IDE changes.
- Anything secret-shaped: `.env*`, `credentials*`, `*_key`, `*.pem`, `save_files/`, `build/`.
- Large binaries or generated assets unless explicitly requested.
- Anything under `creations/` that is gitignored by the root `.gitignore` — those are private per-implementation projects that may live in their own repos. If the user is actually working inside one of those private creations, the skill still applies but runs against that creation's own git repo / own remote; do not try to commit it into the engine repo from a worktree that doesn't own the files.

If the diff includes any of the above, warn the user before committing.

### 6. Create the commit

Pass the message via HEREDOC:

```bash
git commit -m "$(cat <<'EOF'
render: match cpu/gpu iso culling bounds

Stage-1 compute was over-culling near the left edge because the cpu-side
visibleIsoViewport used the raw camera pos while the gpu used the
trixel-offset-corrected one. Align both to the offset-corrected form.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

If the commit fails due to a pre-commit hook, **do not** `--amend`. Fix the
underlying issue, re-stage, and create a **new** commit.

### 7. Push the feature branch

```
git push -u origin HEAD
```

`-u` sets upstream on the first push of a new branch; on subsequent pushes,
plain `git push` works.

### 8. Open the PR

Use `gh pr create`. For the single-PR flow, target is `master`. Pass the
body via HEREDOC so the Markdown renders correctly:

```bash
gh pr create --base master --title "render: match cpu/gpu iso culling bounds" --body "$(cat <<'EOF'
## Summary
- Stage-1 compute vs cpu iso culling bounds were out of sync; fixed.
- Now both paths use the trixel-offset-corrected camera pos.

## Test plan
- [ ] `IRShapeDebug` renders without left-edge culling pop at zoom ≥ 2
- [ ] `render-debug-loop` screenshots diff-clean against baseline

## Notes for reviewer
Check `engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl` vs
`engine/math/src/ir_math.cpp` `visibleIsoViewport`.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Title should match the commit title when the PR is a single commit. For
multi-commit PRs, use a broader title that covers the series.

**Stack-aware override:** when the current task is part of a
`fleet-claim stack`, compute the base via `stack-base` and record the
resulting PR via `stack-set-pr`. When `$base` is a feature branch
(i.e. not `master`), also pass `--label "fleet:stacked"` so the merger
can filter stacked PRs via label without an extra `gh pr view --json
baseRefName` call per candidate:

```bash
base=$(fleet-claim stack-base <agent> <task-id>)
labels=(--label "fleet:wip")
if [[ "$base" != "master" ]]; then
    labels+=(--label "fleet:stacked")
fi
pr_url=$(gh pr create --base "$base" \
    --title "T-<NNN>: <short title>" \
    --body "$(cat <<'EOF'
## Summary
- <what this task does>

## Stack context
This PR is part of a stack. Reviewers: review this PR on its own; the
chain is coordinated in the PR body's "Stacked on" line.

Stacked on: <previous PR URL, or "master" for the first PR>
Full chain: T-<A>, T-<B>, T-<C>

## Test plan
- [ ] <task-specific checks>

Closes #<issue-N>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)" "${labels[@]}")
fleet-claim stack-set-pr <agent> <task-id> "$(git branch --show-current)" "$pr_url"
```

The `Stacked on:` line is the reviewer's primary signal that earlier
PRs are prerequisites. The `Full chain:` line helps them find the
siblings if they want cross-context. The merger reads `baseRefName`
directly for correctness decisions; `fleet:stacked` is a derived
convenience label for human visibility and cheap filtering.

**Cursor stack override:** when the current branch has a
`cursor-stack-base` git config set (see "Cursor stack mode" section
above), use that as the PR base instead of `master`. No `fleet-claim`
calls and no `fleet:stacked` label — cursor stack mode is human-
managed.

```bash
parent_branch=$(git config --get branch."$(git branch --show-current)".cursor-stack-base)
if [[ -n "$parent_branch" ]]; then
    parent_pr_url=$(gh pr list --head "$parent_branch" --state all \
        --json url -q '.[0].url' --limit 1)
    parent_pr_ref="${parent_pr_url:-$parent_branch (no PR yet)}"
    gh pr create --base "$parent_branch" \
        --title "<scope>: <title>" \
        --body "$(cat <<EOF
## Summary
- <what this slice does>

## Stack context
Stacked on: $parent_pr_ref

When the parent PR merges, change this PR's base to \`master\`
(via the GitHub UI or \`gh pr edit <N> --base master\`).

## Test plan
- [ ] <slice-specific checks>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
fi
```

The `Stacked on:` line is the reviewer's signal that an earlier PR is
a prerequisite. Cursor stack mode does not record a `Full chain:`
line — chains are short and the link-walk via `Stacked on:` is
sufficient.

The `gh pr list --head` and `gh pr create` calls both need keychain
access; on macOS Cursor's Bash sandbox, run them with `all`
permissions.

### 8b. Tag the host the PR was authored on

After `gh pr create` succeeds, stamp a `fleet:authored-on-<host>`
label so the reviewer knows which backend the author already
implicitly smoke-tested. Per the engine `CLAUDE.md` "Verifying
render changes" section, render-PR authors are expected to build
and run the demo on their host before opening — so authoring on a
host is reasonable evidence that host's smoke is at least
baseline-validated.

This is the **author's host fact**, not a state label. Reviewers
read it to skip the matching `fleet:needs-<host>-smoke` label so
the author's host doesn't get tagged for redundant validation.
The OTHER host's smoke label (if needed per the diff) still gets
added — backend drift between OpenGL and Metal is the whole point
of cross-host validation.

```bash
host_kernel=$(uname -s)
case "$host_kernel" in
    Linux)  host_label="fleet:authored-on-linux" ;;
    Darwin) host_label="fleet:authored-on-macos" ;;
    *)      host_label="" ;;  # unknown host (Windows native, etc.) — skip
esac
if [[ -n "$host_label" ]]; then
    gh pr edit <N> --add-label "$host_label"
fi
```

This applies to **all** PRs (engine and game, render-touching or
not). The label is cheap and consistent — having it always present
makes the reviewer's logic simple ("subtract author host from smoke
labels"). Game PRs don't currently get smoke labels, so the host
label is just informational there; that's fine.

If the host kernel is neither `Linux` nor `Darwin` (Windows
native via MSYS2, etc.), skip the label — the cross-host smoke
flow is OpenGL-on-Linux vs Metal-on-macOS, and a Windows author
doesn't fit either bucket cleanly.

### 9. Report the result

Reply with a compact summary:

- The branch name you pushed.
- The PR URL returned by `gh pr create`.
- The list of files that went into the commit(s).
- Confirmation that push + PR both succeeded.
- Any files you intentionally did **not** stage (and why).

**Do not** start new work in the same worktree after opening the PR. The
`start-next-task` skill handles resetting the worktree to a fresh branch off
master for the next chunk — tell the user to invoke it (or invoke it yourself
if the user already asked for the next task).

## Rebase guard

**Before rebasing this PR branch onto origin/master**, always capture
the current diff first. Git's 3-way merge can silently drop hunks
from non-conflicting regions of a file when a different region of the
same file has a conflict — no conflict markers, no warning in the
rebase output.

Do NOT use `>` redirects to `/tmp/` (or any path) — Claude Code's Bash
tool blocks shell redirects regardless of destination. Both snapshots
live in your conversation context as Bash output; large diffs auto-
persist to a `<persisted-output>` link the next iteration can Read.
(Same rule that role-merger.md uses for its rebase guard.)

### Pre-capture (do this BEFORE `git rebase origin/master`)

Run `git diff origin/master` and keep the output in your conversation
context — you'll compare it to the post-rebase snapshot below.

### Rebase and resolve conflicts

Run `git rebase origin/master`. Resolve any conflict markers normally.

### Post-capture and comparison

Run `git diff origin/master` again. Compare to the pre-capture above:
look for lines beginning with `+` in the pre-capture that are absent
from the post-capture. Each such gap is a silently dropped hunk that
must be manually re-applied before committing.

For huge diffs that don't fit cleanly in context, both snapshots get
auto-persisted by Claude Code; Read the persisted-output files to
diff them with the Read tool's `offset`/`limit`.

### If the pre-capture was skipped

1. Run `git reflog --since=2.hours.ago` to confirm a rebase happened.
2. Compare `git diff origin/master` against the branch's last pushed state:
   `git diff origin/<branch-name>` shows what changed since the last push.
3. Look for missing code blocks based on the PR's commit messages and
   description. Re-apply any that are absent.

## Anti-patterns

- ❌ Committing to `master`. Always branch first.
- ❌ `git add -A` / `git add .` — risk of committing secrets or build junk.
- ❌ Amending a previous commit without being asked.
- ❌ Force-pushing master. Never.
- ❌ Skipping hooks (`--no-verify`) unless the user explicitly requests it.
- ❌ Bypass-pushing to master with admin — the new workflow uses PRs and
  reviewer agents. If you feel tempted to bypass, something upstream is
  broken; flag it to the user instead.
- ❌ Opening a PR without running `simplify` first.
- ❌ Continuing to commit on the same feature branch after opening its PR
  unless the user explicitly asks for it — otherwise use `start-next-task`.
- ❌ Leaking game references into an engine PR (file paths under
  `creations/game/`, `jakildev/irreden`, game task IDs, game design
  language). Engine repo is public; game repo is private. See engine
  `CLAUDE.md` "Cross-repo information isolation".

## Recovery

If the working tree has changes that clearly weren't made during this
session (e.g. a half-finished refactor in an unrelated file), ask the user
whether to include them before staging. When in doubt, stage only the files
you know the session touched.

If `gh pr create` fails because a PR already exists for the branch, report
the existing PR URL and tell the user — don't try to force a new one.
