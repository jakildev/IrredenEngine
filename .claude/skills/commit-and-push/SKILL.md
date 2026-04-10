---
name: commit-and-push
description: >-
  Stage, commit, and push Irreden Engine changes to origin/master in the
  project's overview-style commit format. Use whenever the user says
  "commit", "commit my changes", "commit and push", "make a PR" (this repo
  commits direct to master, no branch), or asks to wrap up a session's
  work. Auto-summarises the diff into a short title + body describing what
  changed and why, preserves the project's lowercase informal tone, and
  always pushes to origin/master when done.
---

# commit-and-push

End-to-end flow for checkpointing a session's work into `master` on the
Irreden Engine repo. This repo's convention is **direct commits to master**
(no feature branches, no PR review) with **overview-style** messages that
describe the session's work, not line-by-line mechanics.

## When to invoke

Trigger this skill whenever the user asks to:

- "commit" / "commit my changes" / "commit and push"
- "make a PR" (there is no PR — this repo commits straight to master)
- "save progress" / "wrap up" / "push this"
- Any phrase that implies checkpointing the current working tree

Do **not** invoke proactively — only when the user explicitly asks.

## Flow

### 1. Gather state (parallel)

Run these three commands in a single Bash-tool batch (multiple tool calls
in one message):

```
git status
git diff --stat && git diff
git log --oneline -10
```

- `git status` — see every modified / untracked path.
- `git diff --stat` then full `git diff` — understand *what* changed in
  each file. The stat gives you a map; the full diff gives you the
  substance. Read enough of the diff to be able to summarise intent, not
  just filenames.
- `git log --oneline -10` — match the tone and level of detail of the
  last several commit titles. Example recent titles:
  - `lots and lots, mostly rendering stuff`
  - `working on rendering optimizations and shape renderer`
  - `build on windows again`
  - `shape_debug: match CPU/GPU shapes exactly + local iso-depth coloring`

  Titles are lowercase, informal, and prefer an area prefix
  (`shape_debug:`, `render:`, `build:`) when the change is scoped.

### 2. Run `simplify` on the dirty changes

**Before** drafting the commit message, invoke the `simplify` skill to
review the staged/unstaged changes for reuse opportunities, quality
issues, and efficiency problems specific to the Irreden Engine, and to
fix any issues it finds. Call it via the Skill tool:

```
Skill: simplify
```

`simplify` will read the same diff you just gathered, look for things
like per-entity `getComponent` inside system ticks, redundant
abstractions, duplicated helpers, naming-convention slips, and other
project-specific smells called out in `CLAUDE.md`, and apply fixes
directly to the working tree.

After `simplify` finishes:

- Re-run `git status` and `git diff --stat` so you see the *post-simplify*
  state. The file list and diff you commit must reflect whatever
  `simplify` modified.
- If `simplify` reported findings it *couldn't* fix automatically
  (e.g. design-level concerns), surface them to the user **before**
  committing and ask whether to proceed, hold, or address them first.
- If the working tree is now clean (simplify reverted everything or
  there were no changes to begin with), stop and tell the user —
  nothing to commit.

### 3. Draft the commit message

Write a message with this shape:

```
<area>: <one-line overview of what the session accomplished>

<1–3 short paragraphs describing the *why* and the non-obvious *what*.
Group related edits. Mention each major subsystem touched. Skip things
that are obvious from the filename alone.>

<If the session unearthed build/runtime gotchas worth remembering, note
them here so the commit doubles as a changelog entry.>

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
```

**Rules:**

- Title ≤ 72 chars, lowercase, no period at end.
- Prefer a scope prefix (`shape_debug:`, `render:`, `engine/voxel:`,
  `build:`, `docs:`) derived from the dominant changed path.
- Body wraps at ~72 chars per line.
- Describe *why* over *what*. The diff tells you what; the commit tells
  future-you why it was worth doing.
- Group multiple unrelated changes only if they were all part of the
  same session. Don't pretend they're one logical change — just list
  them as separate paragraphs.
- **Always** include the `Co-Authored-By` trailer exactly as shown.

### 4. Stage files

Add the specific paths that should be part of this commit. **Never** use
`git add -A` or `git add .`:

```
git add <path1> <path2> ...
```

**Exclusions:**

- `.claude/` — worktree metadata, never tracked.
- `.vscode/` unless the user explicitly asked for IDE changes.
- Any path that looks like a secret: `.env*`, `credentials*`,
  `*_key`, `*.pem`, `save_files/`, `build/`.
- Large binaries or generated assets unless explicitly requested.

If the diff includes any of the above, warn the user before committing
and let them decide.

### 5. Create the commit

Always pass the message via HEREDOC so multi-line bodies format
correctly:

```bash
git commit -m "$(cat <<'EOF'
shape_debug: match CPU/GPU shapes exactly + local iso-depth coloring

Snap-mode GPU path now walks the full iso-column lattice... [body]

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>
EOF
)"
```

If the commit fails due to a pre-commit hook, **do not** `--amend`.
Instead: fix the underlying issue, re-stage, and create a NEW commit.

### 6. Push to origin/master

This repo's workflow is direct-push to master. After the commit
succeeds, push immediately:

```
git push origin master
```

The master branch has a "PRs required" rule that the user bypasses with
admin — expect a `remote: Bypassed rule violations` notice. That is
normal; the push still goes through.

### 7. Report the result

Reply with a compact summary:

- The new commit SHA (short form).
- The list of files that went into it.
- Confirmation that `git push` succeeded and how far ahead the branch
  is (or isn't) of origin.
- Any files that were intentionally **not** committed (e.g. `.claude/`,
  IDE settings) so the user knows what's still pending.

## Anti-patterns

- ❌ `git add -A` / `git add .` — risk of committing secrets or stale
  build outputs.
- ❌ Amending a previous commit without being asked. This repo's
  convention is new commits, not amends.
- ❌ Skipping hooks (`--no-verify`, `--no-gpg-sign`) unless the user
  explicitly requests it. Fix the hook failure instead.
- ❌ Commit titles like "fix bug" or "update files" — always scope
  and describe the session's intent.
- ❌ Force-pushing master. Never.
- ❌ Opening a GitHub PR. This repo doesn't use them; the user
  bypasses the PR rule on purpose.
- ❌ Creating a branch before committing. Commit on whatever branch
  you're already on (usually `master`).

## Recovery

If you find the working tree dirty with changes that clearly weren't
made during this session (e.g. a half-finished refactor in an unrelated
file), ask the user whether to include them before staging. When in
doubt, stage only the files you know the session touched.
