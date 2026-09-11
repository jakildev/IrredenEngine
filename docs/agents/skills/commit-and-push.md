# commit-and-push — shared flow

Stage, commit, push a feature branch, and open a PR against the repo's
default branch. Never commit to the default branch. Invoke only when the
user asks.

Each repo's `.claude/skills/commit-and-push/SKILL.md` is a thin wrapper that
points here and answers the delta keys below
([`docs/design/skill-sharing.md`](../../design/skill-sharing.md)). A step
that needs a repo-specific value names its **delta key** in bold.

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
| **sha-pin token** | The placeholder the **screenshot skill** emits in place of a commit SHA; step 8 substitutes the real one. | `@COMMIT_SHA@` |
| **info-isolation check** | The cross-repo leakage scan. | `docs/agents/CLAUDE-BASELINE.md` §"Cross-repo information isolation" |
| **co-author trailer** | The commit trailer. | `Co-Authored-By: Claude <noreply@anthropic.com>` (exact form per the harness system prompt) |
| **procedures** | Repo-specific step expansions. | the engine wrapper's `procedures/*.md` |

---

## Preconditions

- You are in your own worktree: the **worktree-assert command** exits 0
  (human-only override `FLEET_ALLOW_MAIN_CLONE=1`).
- `git rev-parse --abbrev-ref HEAD` is not the **default branch**.
- `gh auth status` succeeds.
- `git status` shows something to commit.

## Mode detection

Priority order; first match wins. The modes are mutually exclusive.

1. **Fleet stack mode** — `<claim-tool> stack-pr-state <worktree>` reports a
   stack claim. One PR per task, chained by `--base`. **procedures**
   `fleet-stack.md`.
2. **Cursor stack mode** — `git config branch.<name>.cursor-stack-base` is
   set (written by `start-next-task`). The PR targets the parent branch.
   **procedures** `cursor-stack.md`.
3. **Single-task mode** (default) — base from `<claim-tool> claim-base
   <issue#>`: the **default branch** for a normal claim or plain human PR,
   the blocker's branch for a stackable claim. **procedures**
   `stackable-on.md`.

---

## Flow

### 1. Gather state

```
git rev-parse --abbrev-ref HEAD
git status
git diff --stat && git diff
git log --oneline -10
```

### 2. Feature branch

On the default branch: `git checkout -b <branch prefix><area>-<topic>`
before staging. Fleet stack mode: `<branch prefix><task-id>-<short-topic>`
off `<claim-tool> stack-base <agent> <task-id>`. Already on a feature
branch: keep it.

With a fleet claim the branch name must encode the claimed issue number —
the claim-liveness matcher ties the open PR to the issue by it, and an
unmatched claim gets judged abandoned and re-worked. The issue number
comes from the claim, not from any lookup:

```bash
<claim-tool> [--repo <ns>] branch-check <issue#>   # exit 0 = ok, 1 = mismatch
git branch -m claude/<issue#>-<short-topic>        # on mismatch, before step 7
```

Skip for a plain human PR with no claimed issue.

### 3. Pre-commit checks

In this order:

- **Rebase guard** (**procedures** `rebase-guard.md`) if the branch was
  rebased this session.
- **Screenshots.** If `git diff --name-only <remote>/<default-branch>...HEAD`
  matches a **visual-file glob** and `docs/pr-screenshots/<branch>/` does
  not exist, run the **screenshot skill** first. Screenshots ship in the
  same commit as the code. Docs/tests/mechanical/build-only diffs skip this.
- **info-isolation check** over staged paths and the PR-body draft.
- Python under `scripts/`: `ruff check --fix scripts/` then `ruff check
  scripts/` must exit 0 (CI-gated; `docs/agents/BUILD.md` §"Python
  (scripts)").
- The **simplify skill**, for every diff type. Afterwards re-run `git
  status`; a clean tree means stop and report. Your next action after
  processing its results is a tool call into step 4, not a prose summary
  that ends the turn.

### 4. Commit message

```
<area>: <one-line overview of what this slice accomplished>

<1–3 short paragraphs: the why, and the non-obvious what>

<co-author trailer>
```

Title ≤ 72 chars, lowercase, no trailing period, scope prefix from the
**scope vocabulary** by dominant changed path; body wraps at ~72. A
subdirectory `CLAUDE.md` that specifies its own shape wins.

### 5. Stage

`git add <path1> <path2> ...` — never `git add -A` / `git add .`. Unless
the user asks, never stage worktree/session state (`.claude/worktrees/`,
`.claude/settings.local.json`), `.vscode/*`, secret-shaped files (`.env*`,
`credentials*`, `*_key`, `*.pem`), `save_files/`, `build/`, large binaries
or generated assets, or anything gitignored under a private creation (it
has its own repo — run this skill there). Warn if the diff contains any.

### 6. Commit

```bash
if git diff --cached --quiet; then
    echo "commit-and-push: refusing to commit — no staged changes." >&2
    exit 1
fi
```

An empty staged tree on a fleet branch means `simplify`/`optimize` stripped
every line — stop and investigate. Pass the message via HEREDOC. If a
pre-commit hook fails, fix, re-stage, and make a **new** commit. Never
`--amend` unless asked.

### 7. Push

```
git push -u <remote> HEAD
```

### 8. Open the PR

Body template and rules: **procedures** `pr-body.md`. Write the body with
the Write tool to the worktree-local, gitignored `.pr-body.md` and pass
`--body-file`. `--body "$var"` is forbidden (an unset variable opens an
empty body and nothing reports it), as is `$(cat <<'EOF' …)` (the
shell-substitution gate) and `printf … > file` (the Bash tool blocks `>`).
If the **screenshot skill** ran, fold its snippet in with every **sha-pin
token** replaced by `git rev-parse HEAD` — the commit from step 6 is the
one whose tree contains the screenshots.

Single-task common case:

```bash
gh pr create --base <default-branch> --title "<scope>: <title>" \
    --body-file .pr-body.md --label fleet:author-<claude|codex>
```

Stack modes and stackable claims resolve the base and edit-or-create per
the **procedures** (`fleet-stack.md`, `cursor-stack.md`, `stackable-on.md`,
`native-stack-link.md`).

No WIP label on a human-ready single PR — fleet reviewers skip WIP. WIP is
the fleet-worker claim lane's marker only.

#### 8a. Checks keyed on `Closes #N` (before `gh pr create`)

Skip when the body has no `Closes #N`, or for the queue-manager role (its
`Closes #` rows are task IDs).

- **Issue match.** `gh issue view <N> --repo <repo> --json
  title,body,comments`. The tokenized title should share words with the PR
  title, branch, or first commit line; an empty intersection is a warning
  (ask in interactive mode; log and verify independently when autonomous).
  Non-blocking — an umbrella issue is a legitimate mismatch.
- **Acceptance evidence.** If the issue states acceptance criteria anywhere
  (a `## Plan` comment's `### Acceptance criteria`, or a bold `**Acceptance
  criteria**` line in the body), the PR body needs a `## Acceptance
  evidence` table with one row per criterion; fewer rows than criteria is
  incomplete. A criterion graded not-shipped downgrades `Closes #N` to
  `Refs #N`.
- **Test plan** items record verification already run, written from
  observed output; no unticked `- [ ]` boxes.
- **Duplicate PR.**
  ```bash
  gh pr list --repo <repo> --state open --json number,body \
      --jq ".[] | select(.body | test(\"(Closes|Fixes|Resolves) #${closes_n}\\\\b\"; \"i\")) | .number"
  ```
  All three verbs, case-insensitive — the same pattern as the worker's
  in-flight check. A hit means stop: add any missing `Closes` line to the
  existing PR and comment the issue with the disposition.
- **Stale diagnostics.**
  ```bash
  git grep -nE "(remove|delete|drop).{0,30}#${closes_n}\b|#${closes_n}\b.{0,30}(remove|delete|drop)" -- ':!docs'
  ```
  A hit is a block annotated "remove when #N closes" about to ship as dead
  code — remove it in a new commit, or re-point it at a live follow-up.
- **Open-PR overlap.** Intersect changed files with open PRs' `files`
  (`gh pr list --state open --json number,files`) and trial-merge each hit
  with `git merge-tree --write-tree <this> <other>`. A hit on
  `docs/agents/**` or `.claude/**` stops the open.

```bash
closes_n="<N from the drafted body>"
fleet-pr-body-lint "$closes_n" --repo <repo> --body-file .pr-body.md \
    --write-issue-json ".issue-${closes_n}.json"
issue_title=$(jq -r '.title' ".issue-${closes_n}.json")
```

Run this snapshot + lint block once per distinct closing issue. Exit 1 means
the evidence section is absent or short; exit 2 means the issue thread or
Markdown could not be read safely. Either status stops publication.
`fleet-pr-body-lint` sets `comments_complete: true` only after pagination
finishes; reuse its snapshot for the title-token check. In multi-issue bodies,
group evidence rows under `### Issue #N`; one issue's rows cannot satisfy
another's criteria.

Tokenize the issue title and the PR title, branch, and first commit line
(lowercase, strip punctuation + stop words), then intersect the remaining
words. An empty intersection is a non-blocking warning: pause for human
acknowledgement in interactive mode; in autonomous mode log it prominently
and verify the number independently before proceeding.

#### 8b. Host label

**procedures** `host-label.md` — applies to every PR.

#### 8c. Provenance

`--label fleet:author-<claude|codex>` on create; after every push that
precedes review, `fleet-runtime stamp <PR> --repo <repo> --runtime
<claude|codex>`. Name the provider that made the change, amendments
included; no Claude co-author trailer on Codex-authored work.

### 9. Report

Branch pushed, PR URL, files committed, confirmation that push and PR both
succeeded, anything intentionally left unstaged. Then stop —
`start-next-task` resets the worktree. After a needs-fix review, your role
file's feedback flow adds the changes-made label.

---

## Recovery

- Changes you did not make this session: ask before staging. Never `git
  stash` to park them — `refs/stash` is shared across worktrees, and
  selective `git add` already leaves them alone.
- `gh pr create` reports an existing PR for the branch: report its URL;
  never force a second.
