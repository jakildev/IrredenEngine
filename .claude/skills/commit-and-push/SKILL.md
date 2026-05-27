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

Do **not** invoke proactively — only when the user explicitly asks.

## Preconditions — do these before anything else

1. **You must NOT be on `master`.** If `git rev-parse --abbrev-ref HEAD` reports
   `master`, stop and warn the user. Branch off master first (see step 2 of
   the flow) — do not commit to master. The old direct-push workflow is
   retired.
2. **`gh` must be authenticated.** If `gh auth status` fails, stop and ask the
   user to run `gh auth login`.
3. **The working tree must have something to commit.** If `git status` is
   clean, tell the user and stop. Also check that the *staged* tree is
   non-empty before calling `git commit` (step 6) — `git diff --cached
   --quiet` exits 0 when nothing is staged; that is an empty-commit
   situation that must be rejected with the error message in step 6.

## Mode detection

This skill has three modes. Detect at the start of the flow, in priority order:

1. **Fleet stack mode** — caller has an active `fleet-claim` stack chain. PRs are chained by `--base` (one PR per task). Detected via `fleet-claim stack-pr-state <worktree>`. See [`procedures/fleet-stack.md`](procedures/fleet-stack.md) for the deltas (branch name with `T-NNN` prefix, `stack-base` lookup for `--base`, `Stack context` body block, `fleet:stacked` label, `stack-set-pr` after PR open).
2. **Cursor stack mode** — current branch has `branch.<name>.cursor-stack-base` git config set (written by `start-next-task` when the human cued stacking). PRs target the parent branch instead of `master`. See [`procedures/cursor-stack.md`](procedures/cursor-stack.md) for the detection check, the `Stacked on:` body line, and the macOS sandbox note.
3. **Single-PR mode (default)** — neither stack signal present. Proceed with the standard flow below.

The two stack modes are mutually exclusive. In single-PR mode, ignore `procedures/fleet-stack.md` and `procedures/cursor-stack.md`; `procedures/pr-body.md` still applies for the step 8 body template.

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
convention slips, and other project-specific smells. It dispatches a
parallel reuse-detection subagent fan-out (five scanners running
concurrently) and surfaces an aggregated reuse-findings block alongside
its main findings. It applies fixes directly.

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

**Empty-commit guard (run before every `git commit` call):** An empty
staged tree produces a misleading commit — it leaves provenance evidence
(a task-titled commit) with no real diff. Reject it explicitly:

```bash
if git diff --cached --quiet; then
    echo "commit-and-push: refusing to commit — no staged changes." >&2
    echo "Either stage real work (git add <files>) or release the claim" >&2
    echo "(fleet-claim release <task-id>) and exit." >&2
    exit 1
fi
```

On fleet branches (`claude/T-NNN-*`), the error is especially important —
an empty commit there leaves misleading task provenance in the repo. If
`simplify` or `optimize` stripped every changed line, stop and investigate
before proceeding.

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

Use `gh pr create`. Body template per mode: [`procedures/pr-body.md`](procedures/pr-body.md).

> **Before calling `gh pr create` in any snippet below, complete step 8a (Closes# cross-check) if the drafted body contains a `Closes #N` line.**

For the **single-PR flow** (default), target is `master`:

```bash
gh pr create --base master --title "<scope>: <title>" --body "$(cat <<'EOF'
## Summary
- <bullet>

## Test plan
- [ ] <check>

Closes #<issue-N>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Title should match the commit title for a single commit; use a broader title
for multi-commit PRs.

**`Closes #N` line (required when the task has an `Issue:` field).** This is
what makes GitHub auto-close the originating issue on merge. Without it the
TASKS.md row still reaps via the title's `T-NNN:` prefix (per
`fleet-tasks-render`'s closed-issue fallback) but the GitHub issue side
stays open and the queue silently accumulates stale items. Omit the line
only when the task's `Issue:` field is `(none)` — cleanup PRs, fleet-tooling
PRs filed without a tracking issue, etc. See
[`procedures/pr-body.md`](procedures/pr-body.md) for the full template and
the stack-mode exceptions (cursor-stack non-leaf PRs deliberately drop it).

**PR labels — `fleet:wip` (important):** For the **default Cursor /
human-ready** single-PR open to `master`, **do not** pass
`--label fleet:wip`. Fleet reviewers **skip** PRs labeled `fleet:wip`
(see `role-sonnet-reviewer.md`). Use `fleet:wip` only in the **fleet
worker** lane: task **claim** / early-work PRs until the author removes
it for review pickup. Step **8b** below adds `fleet:authored-on-*`
where appropriate — that is unrelated and still applies. The
**Stack-aware** block still uses `fleet:wip` for stacked fleet-worker
PRs; that is intentional for that workflow.

**Fleet stack override:** when the current task is part of a
`fleet-claim stack`, compute the base via `stack-base` and record the
resulting PR via `stack-set-pr`. When `$base` is a feature branch
(i.e. not `master`), also pass `--label "fleet:stacked"` so the merger
can filter stacked PRs via label without an extra `gh pr view --json
baseRefName` call per candidate. Body adds a `## Stack context` block —
see [`procedures/pr-body.md`](procedures/pr-body.md) fleet stack mode:

```bash
base=$(fleet-claim stack-base <agent> <task-id>)
labels=(--label "fleet:wip")
if [[ "$base" != "master" ]]; then
    labels+=(--label "fleet:stacked")
fi
pr_url=$(gh pr create --base "$base" \
    --title "T-<NNN>: <short title>" \
    --body "$(cat <<'EOF'
<fleet stack body — see procedures/pr-body.md fleet stack mode>
EOF
)" "${labels[@]}")
fleet-claim stack-set-pr <agent> <task-id> "$(git branch --show-current)" "$pr_url"
```

**Cursor stack override:** when the current branch has a
`cursor-stack-base` git config set (see [`procedures/cursor-stack.md`](procedures/cursor-stack.md)
for the detection rule and full deltas), use that as the PR base instead of
`master`. No `fleet-claim` calls and no `fleet:stacked` label. Body adds a
`## Stack context` block — see [`procedures/pr-body.md`](procedures/pr-body.md)
cursor stack mode:

```bash
parent_branch=$(git config --get branch."$(git branch --show-current)".cursor-stack-base)
if [[ -n "$parent_branch" ]]; then
    parent_pr_url=$(gh pr list --head "$parent_branch" --state all \
        --json url -q '.[0].url' --limit 1)
    parent_pr_ref="${parent_pr_url:-$parent_branch (no PR yet)}"
    gh pr create --base "$parent_branch" \
        --title "<scope>: <title>" \
        --body "$(cat <<EOF
<cursor stack body — see procedures/pr-body.md cursor stack mode; substitute $parent_pr_ref>
EOF
)"
fi
```

### 8a. Cross-check `Closes #N` before PR creation

When the PR body includes a `Closes #N` line, run this cross-check **before**
calling `gh pr create`. It catches the class of error where the worker writes
the wrong issue number, causing GitHub to auto-close an unrelated issue on merge.

```bash
closes_n="<N from the drafted body>"
issue_title=$(gh issue view "$closes_n" --repo jakildev/IrredenEngine \
    --json title --jq '.title' 2>/dev/null || echo "")
```

Tokenize both strings: lowercase, strip punctuation, remove stop words
(`a`, `an`, `the`, `for`, `of`, `in`, `to`, `at`, `on`, `and`, `or`,
`not`, `is`, `via`, `from`, `with`, `add`, `—`, `-`). Intersect the
remaining word sets against the PR title + branch name + first commit line.

If the intersection is **empty**, surface this warning. In interactive
mode (Cursor / human-in-the-loop), pause for human acknowledgement. In
autonomous mode (fleet worker / author role), log the warning prominently
and verify the issue number independently before proceeding:

```
⚠️  Closes-crosscheck: issue #<N> title is:
      "<issue title>"
    This PR's scope appears to be:
      "<PR title>"
    Zero keyword overlap — verify the issue number is correct.
    (Non-blocking: proceed only if the number is intentional.)
```

The check is non-blocking. If the worker has confirmed the number is
correct (e.g. the PR deliberately closes an umbrella or tracking issue
whose title differs from the implementation PR title), proceed after
acknowledgement.

Skip this check only when:
- The PR body has no `Closes #N` line (cleanup PRs, fleet-tooling PRs
  filed without a tracking issue).
- You are the queue-manager role (maintenance-sync PRs use `Closes #` for
  task rows, not GitHub issues — the queue-manager is exempt because its
  `Closes #` semantics differ from the standard flow).

**Incident note (2026-05):** PR #1212 (T-379 PARALLEL_FOR bulk migration)
shipped with `Closes #1215` instead of `Closes #1196`. GitHub auto-closed
an unrelated issue (#1215 "fleet: scout reads issues instead of TASKS.md")
and left the correct issue (#1196) open for re-claiming. This cross-check
catches that class of error before the PR is created.

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

- Amending a previous commit without being asked.
- Leaking game references into an engine PR — see `docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation".

## Recovery

If the working tree has changes that clearly weren't made during this
session (e.g. a half-finished refactor in an unrelated file), ask the user
whether to include them before staging. When in doubt, stage only the files
you know the session touched.

If `gh pr create` fails because a PR already exists for the branch, report
the existing PR URL and tell the user — don't try to force a new one.
