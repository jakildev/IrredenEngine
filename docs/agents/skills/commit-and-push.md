# commit-and-push — shared flow

The canonical `commit-and-push` flow: stage, commit, push a feature branch,
and open a PR against the repo's default branch. Assumes the parallel-agent
workflow where work happens on short-lived feature branches, another agent
reviews the PR, and the human merges. **NEVER commit directly to the default
branch.**

Every repo that runs a fleet keeps its
`.claude/skills/commit-and-push/SKILL.md` as a thin wrapper that points here
and supplies only its **deltas** (below) plus repo-specific procedure files.
See [`docs/design/skill-sharing.md`](../../design/skill-sharing.md) for the
mechanism. Wherever a step needs a repo-specific value it names a **delta
key** in bold.

Do **not** invoke proactively — only when the user explicitly asks.

---

## Repo deltas this flow needs

| Delta key | What it is | Engine value |
|---|---|---|
| **repo** | The `gh --repo` slug. | `jakildev/IrredenEngine` |
| **default branch** | The PR base in the common case. | `master` |
| **remote** | Git remote pushed to. | `origin` |
| **branch prefix** | New feature-branch prefix. | `claude/` |
| **worktree-assert command** | Fails if you're in the shared main clone. | `fleet-assert-worktree` |
| **claim tool** | The fleet claim/stack helper. | `fleet-claim` |
| **simplify skill** | The repo's pre-commit quality skill. | `simplify` |
| **scope vocabulary** | Commit-title scope prefixes. | `render:`, `engine/voxel:`, `game/nav:`, `build:`, `docs:` |
| **visual-file globs** | Paths whose change requires screenshots. | `engine/render/`, `engine/prefabs/irreden/render/`, `*.glsl`, `*.metal`, `creations/demos/*/src/**`, `creations/demos/*/main*.cpp` |
| **screenshot skill** | The repo's screenshot-capture skill. | `attach-screenshots` |
| **sha-pin token** | The literal placeholder the **screenshot skill** emits in place of a deletable branch ref; this flow substitutes it with the real commit SHA once one exists (see step 8). | `@COMMIT_SHA@` |
| **info-isolation check** | The cross-repo leakage scan. | `docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation" |
| **co-author trailer** | The commit trailer. | `Co-Authored-By: Claude <noreply@anthropic.com>` (exact form per the harness system prompt) |
| **procedures** | Repo-specific step expansions. | the engine wrapper's `procedures/*.md` |

---

## Preconditions — do these before anything else

1. **Confirm you are in your own worktree, not the shared main clone.** Run
   the **worktree-assert command** (optionally with your worktree basename).
   If it exits non-zero, STOP and `cd` into your worktree before doing
   anything — a commit from the shared main clone collides with other agents
   on that checkout. Human-only override: `FLEET_ALLOW_MAIN_CLONE=1`.
2. **You must NOT be on the default branch.** If `git rev-parse
   --abbrev-ref HEAD` reports the **default branch**, stop and warn. Branch
   first (flow step 2) — do not commit to it.
3. **`gh` must be authenticated.** If `gh auth status` fails, ask the user
   to run `gh auth login`.
4. **The working tree must have something to commit.** If `git status` is
   clean, tell the user and stop. Also check the *staged* tree is non-empty
   before `git commit` (step 6): `git diff --cached --quiet` exits 0 when
   nothing is staged — reject that empty-commit case.

---

## Mode detection

Three modes, detected at the start in priority order:

1. **Fleet stack mode** — caller has an active **claim tool** stack chain.
   One PR per task, chained by `--base`. Detected via `<claim-tool>
   stack-pr-state <worktree>`. See the **procedures** `fleet-stack.md` for
   the deltas (issue-number-prefixed branch name, `stack-base` lookup,
   `Stack context` body block, stacked label, `stack-set-pr` after PR open).
2. **Cursor stack mode** — current branch has `branch.<name>.cursor-stack-base`
   git config (written by `start-next-task` when the human cued stacking).
   PRs target the parent branch instead of the default branch. See the
   **procedures** `cursor-stack.md`.
3. **Single-task mode (default)** — neither stack signal. The base is
   resolved via `<claim-tool> claim-base <issue#>`: the default branch for a
   normal claim or a plain human PR, or the blocker's branch for an
   opportunistic stackable claim (which also gets the stacked label). See
   the **procedures** `stackable-on.md`.

The modes are mutually exclusive and checked in this order. In single-task
mode ignore the stack-mode procedures; the PR-body template and the
base+label resolution still apply.

---

## Flow

### 1. Gather state (parallel)

Run in one Bash-tool batch:

```
git rev-parse --abbrev-ref HEAD
git status
git diff --stat && git diff
git log --oneline -10
```

The branch name tells you whether you're already on a feature branch;
`git status`/`git diff` tell you **what** changed and **why** (for the PR
body); `git log` lets you match the tone of recent titles.

### 2. Ensure you're on a feature branch

If you're on the default branch, derive a short kebab-case name prefixed
`<branch prefix><area>-<topic>` and create it **before** staging:

```bash
git checkout -b <branch prefix><area>-<topic>
```

**In fleet stack mode** use `<branch prefix><task-id>-<short-topic>` and
branch off the base from `<claim-tool> stack-base <agent> <task-id>` (the
default branch for the first task, else the previous task's branch). See the
**procedures** `fleet-stack.md`.

If you're already on a feature branch, use it — don't rename mid-session.

### 3. Pre-commit checks and `simplify`

**First**, run the **rebase guard** (**procedures** `rebase-guard.md`): if
you saved a `git diff <remote>/<default-branch>` snapshot before rebasing,
compare it now against a fresh one to catch silently-dropped hunks. If you
have no snapshot, check `git reflog --since=2.hours.ago` for a recent rebase
and inspect manually if found.

**Second**, check whether the diff touches visual/render files:

```bash
git diff --name-only <remote>/<default-branch>...HEAD
```

If any path matches a **visual-file glob** AND
`docs/pr-screenshots/<current-branch>/` does not yet exist, stop and prompt
the worker to run the **screenshot skill** before proceeding. Screenshots
ship in the same commit as the code change. Skip the prompt if the diff is
purely docs/tests/mechanical/build, or the screenshots dir already exists
for this branch.

**Then**, run the **info-isolation check**: scan staged paths and the PR-body
draft for the cross-repo leakage tokens listed in the **info-isolation
check** reference. Surface any match before committing.

**Then**, if the diff touches any Python under `scripts/` (the fleet
automation or the render / perf / gui harnesses), run the **Python lint** —
the analogue of the C++ `format-changed`/`lint` targets:

```bash
ruff check --fix scripts/   # autofix import-order / unused imports
ruff check scripts/         # must exit 0 — gated in CI (quality.yml)
```

Resolve any remaining findings (line-length, ambiguous names, bare asserts)
before committing; the CI `Python lint` step rejects the PR otherwise. See
BUILD.md § "Python (scripts)" for the rule set and install.

**Then** invoke the **simplify skill** to review the dirty changes for
reuse, quality, and efficiency issues. It reads the same diff and applies
fixes directly. **Always run it, regardless of file types** — no doc-only or
small-change carve-out (the doc-side checks earn their keep on
markdown-heavy diffs too).

After `simplify` finishes: re-run `git status`/`git diff --stat`; surface
any findings it couldn't auto-fix and ask whether to proceed; if the tree is
now clean, stop and report (nothing to commit).

> **Autonomous-fleet guardrail (post-simplify boundary):** after processing
> the simplify results, your VERY NEXT action must be a **tool call** —
> either advancing to step 4 or confirming the clean-tree stop. Do not emit
> only prose summarizing simplify and end the turn without a tool call.
> Phrases like "Returning to commit-and-push" without an immediate following
> tool call are the signal you're about to drop the flow; issue the tool
> call instead.

### 4. Draft the commit message

```
<area>: <one-line overview of what this slice accomplished>

<1–3 short paragraphs describing the *why* and the non-obvious *what*.
Group related edits. Mention each major subsystem touched. Skip things
obvious from filenames.>

<co-author trailer>
```

If the work is entirely inside a subdirectory with its own `CLAUDE.md`
specifying a different commit-message shape, prefer that — read the nearest
`CLAUDE.md` first.

**Rules:** title ≤ 72 chars, lowercase, no trailing period; prefer a scope
prefix from the **scope vocabulary** derived from the dominant changed path;
body wraps at ~72 chars; describe *why* over *what*; always include the
**co-author trailer**.

### 5. Stage files

Add specific paths. **Never** `git add -A` / `git add .`:

```
git add <path1> <path2> ...
```

**Exclusions — never stage unless the user explicitly asks:** worktree/
session state (`.claude/worktrees/`, `.claude/settings.local.json`),
`.vscode/*`, secret-shaped files (`.env*`, `credentials*`, `*_key`, `*.pem`,
`save_files/`, `build/`), large binaries/generated assets, and anything
gitignored under a private creation directory (those live in their own
repos; run the skill against that creation's own repo/remote). If the diff
includes any of the above, warn before committing.

### 6. Create the commit

**Empty-commit guard (run before every `git commit`):** an empty staged
tree produces a misleading commit (task-titled, no diff). Reject it:

```bash
if git diff --cached --quiet; then
    echo "commit-and-push: refusing to commit — no staged changes." >&2
    echo "Either stage real work (git add <files>) or release the claim" >&2
    echo "(<claim-tool> release <task-id>) and exit." >&2
    exit 1
fi
```

On fleet branches an empty commit leaves misleading task provenance — if
`simplify`/`optimize` stripped every changed line, stop and investigate.

Pass the message via HEREDOC. If the commit fails on a pre-commit hook, **do
not** `--amend` — fix the issue, re-stage, create a **new** commit.

### 7. Push the feature branch

```
git push -u <remote> HEAD
```

`-u` sets upstream on the first push; plain `git push` works thereafter.

### 8. Open the PR

Use `gh pr create`. Body template: **procedures** `pr-body.md`.

> Before calling `gh pr create`, complete step 8a (Closes-crosscheck) if the
> drafted body contains a `Closes #N` line.

**Substitute the sha-pin token.** If the **screenshot skill** ran earlier in
this pass, its markdown snippet embeds the **sha-pin token** in place of a
commit SHA (the screenshots were staged before a commit existed to pin to).
Immediately before calling `gh pr create`, replace every occurrence of the
**sha-pin token** in the drafted PR body with the just-pushed commit:

```bash
pr_body="${pr_body//<sha-pin token>/$(git rev-parse HEAD)}"
```

`HEAD` at this point is the commit created in step 6 and pushed in step 7,
so the substituted SHA is the one whose tree actually contains the
screenshots under the **screenshot skill**'s output path. Skip this
substitution when the body has no **sha-pin token** occurrence (no
screenshots were attached this pass).

For the **single-task flow** (default), resolve the base via `<claim-tool>
claim-base` — the **default branch** for a normal claim or plain human PR
(common case below), or the blocker's branch for a stackable claim (also
gets the stacked label). The idempotent edit-or-create and the `Stacked on:`
body line live in the **procedures** `stackable-on.md`. Common case:

```bash
gh pr create --base <default-branch> --title "<scope>: <title>" --body "$(cat <<'EOF'
## Summary
- <bullet>

## Test plan
- [ ] <check>

Closes #<issue-N>

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

**`Closes #N` line** (required when the task has an `Issue:` field) is what
makes the tracker auto-close the originating issue on merge. Omit only when
the `Issue:` field is `(none)` (cleanup PRs, fleet-tooling PRs). See
**procedures** `pr-body.md` for the full template and the stack-mode
exceptions (cursor-stack non-leaf PRs deliberately drop it).

**PR labels — WIP (important):** for the default human-ready single-PR open
to the default branch, **do not** add the WIP label — fleet reviewers skip
WIP PRs. Use the WIP label only in the fleet-worker lane (claim/early-work
PRs until the author removes it for review pickup). Step 8b's host label is
unrelated and still applies.

**Fleet stack override** and **cursor stack override** apply their base +
label + `Stack context` body deltas per the **procedures** `fleet-stack.md`
/ `cursor-stack.md`.

### 8a. Cross-check `Closes #N` before PR creation

When the body includes `Closes #N`, cross-check **before** `gh pr create` to
catch a wrong issue number that would auto-close an unrelated issue:

```bash
closes_n="<N from the drafted body>"
issue_title=$(gh issue view "$closes_n" --repo <repo> \
    --json title --jq '.title' 2>/dev/null || echo "")
```

Tokenize both strings (lowercase, strip punctuation + stop words), intersect
the remaining word sets against PR title + branch name + first commit line.
If the intersection is **empty**, surface a warning: pause for human
acknowledgement in interactive mode; in autonomous mode log it prominently
and verify the number independently before proceeding. The check is
non-blocking — proceed if the number is intentional (e.g. an umbrella).

Skip when the body has no `Closes #N` line, or for the queue-manager role
(its `Closes #` rows mean task IDs, not tracker issues).

**Stale-diagnostic check (same trigger):** for each `Closes #N`, grep the
branch for annotations that promise removal when that issue closes —

```bash
git grep -nE "(remove|delete|drop).{0,30}#${closes_n}\b|#${closes_n}\b.{0,30}(remove|delete|drop)" -- ':!docs'
```

A hit means a diagnostic block annotated "remove when #N closes" is about
to ship to the default branch as dead code in the very PR that closes #N.
Remove the block (new commit) before opening the PR; if it must outlive
this PR, re-point the annotation at a live follow-up issue instead.

### 8b. Tag the host the PR was authored on

See the **procedures** `host-label.md` for the shell snippet and scope
rules (applies to all PRs; the host label tells the reviewer which backend
the author already implicitly validated).

### 9. Report the result

**Fix-push convention:** if pushing a fix in response to a needs-fix review,
add the changes-made label after the push so the reviewer re-verifies. Your
role file's feedback-fix flow handles this step — don't skip it.

Reply with a compact summary: the branch you pushed; the PR URL; the files
in the commit(s); confirmation push + PR both succeeded; any files you
intentionally did not stage (and why).

**Do not** start new work in the same worktree after opening the PR.
`start-next-task` handles resetting to a fresh branch — tell the user to
invoke it (or invoke it yourself if they asked for the next task).

---

## Anti-patterns

- Amending a previous commit without being asked.
- Leaking another repo's references into this repo's PR — see the
  **info-isolation check** reference.

## Recovery

If the working tree has changes that clearly weren't made this session, ask
before staging. When in doubt, stage only files you know the session
touched. **Do not `git stash` to "park" unrelated WIP** — selective
`git add <path>` already leaves it untouched, and `refs/stash` is shared
across all worktrees, so a positional `git stash pop` can apply another
agent's stash into your tree.

If `gh pr create` fails because a PR already exists for the branch, report
the existing PR URL — don't force a new one.
