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

## Mode detection

This skill has three modes. Detect at the start of the flow, in priority order:

1. **Fleet stack mode** — caller has an active `fleet-claim` stack chain. PRs are chained by `--base` (one PR per task). Detected via `fleet-claim stack-pr-state <worktree>`. See [`procedures/fleet-stack.md`](procedures/fleet-stack.md) for the deltas (branch name with `T-NNN` prefix, `stack-base` lookup for `--base`, `Stack context` body block, `fleet:stacked` label, `stack-set-pr` after PR open).
2. **Cursor stack mode** — current branch has `branch.<name>.cursor-stack-base` git config set (written by `start-next-task` when the human cued stacking). PRs target the parent branch instead of `master`. See [`procedures/cursor-stack.md`](procedures/cursor-stack.md) for the detection check, the `Stacked on:` body line, and the macOS sandbox note.
3. **Single-PR mode (default)** — neither stack signal present. Proceed with the standard flow below.

The two stack modes are mutually exclusive. In single-PR mode, ignore the procedures/ files and follow the Flow as written.

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
- **In fleet stack mode** (see [`procedures/fleet-stack.md`](procedures/fleet-stack.md)): use
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

**First**, run the **Rebase guard** check (full procedure: [`procedures/rebase-guard.md`](procedures/rebase-guard.md)): if you saved a pre-capture earlier in this conversation (a `git diff origin/master` snapshot before rebasing), compare it now against a fresh `git diff origin/master`. If you don't have that snapshot in context, check `git reflog --since=2.hours.ago` for a recent rebase entry; if found, warn and inspect manually before proceeding.

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

**Then**, run the cross-repo information-isolation check per
`docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation":
scan staged paths with `git diff --cached --name-only -- 'creations/game/'`
and the PR body draft with the Grep tool for the game-leakage tokens
listed there. Surface any match to the user before committing.

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

Co-Authored-By: Claude <noreply@anthropic.com>
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
- Always include the `Co-Authored-By` trailer; the harness system prompt specifies the exact form with the current model version.

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

Co-Authored-By: Claude <noreply@anthropic.com>
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

**PR labels — `fleet:wip` (important):** For the **default Cursor /
human-ready** single-PR open to `master`, **do not** pass
`--label fleet:wip`. Fleet reviewers **skip** PRs labeled `fleet:wip`
(see `role-sonnet-reviewer.md`). Use `fleet:wip` only in the **fleet
worker** lane: task **claim** / early-work PRs until the author removes
it for review pickup. Step **8b** below adds `fleet:authored-on-*`
where appropriate — that is unrelated and still applies. The
**Stack-aware** block still uses `fleet:wip` for stacked fleet-worker
PRs; that is intentional for that workflow.

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
`cursor-stack-base` git config set (see [`procedures/cursor-stack.md`](procedures/cursor-stack.md)
for the detection rule and full deltas), use that as the PR base
instead of `master`. No `fleet-claim` calls and no `fleet:stacked`
label — cursor stack mode is human-managed.

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

See [`procedures/host-label.md`](procedures/host-label.md) for the shell snippet and scope rules (applies to all PRs; Windows-native authors skip the label).

### 9. Report the result

**Fix-push convention:** if you are calling commit-and-push to push a
fix in response to `fleet:needs-fix` feedback, add `fleet:changes-made`
to the PR after the push so the reviewer knows new commits arrived and
should re-verify. Your role file's feedback-fix flow handles the
`fleet:changes-made` label step; check that step and do not skip it.
Without `fleet:changes-made`, the reviewer has no signal to pick the
PR back up and re-review.

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
- ❌ Leaking game references into an engine PR — see `docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation".

## Recovery

If the working tree has changes that clearly weren't made during this
session (e.g. a half-finished refactor in an unrelated file), ask the user
whether to include them before staging. When in doubt, stage only the files
you know the session touched.

If `gh pr create` fails because a PR already exists for the branch, report
the existing PR URL and tell the user — don't try to force a new one.
