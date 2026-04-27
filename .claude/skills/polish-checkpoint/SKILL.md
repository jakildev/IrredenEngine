---
name: polish-checkpoint
description: >-
  Run a mid-session quality checkpoint without committing. Mirrors the
  pre-commit phase of `commit-and-push` (simplify the dirty tree,
  format, verify the build for the touched target) but stops before
  any git operations — no staging, no commit, no push, no PR. Use
  when the user says "checkpoint", "polish for now", "verify what I
  have", "clean up but don't commit", "self-review without shipping",
  "polish + verify", or otherwise wants confidence that the working
  tree is clean and the build is green before continuing to iterate.
  Designed for the Cursor / human-in-the-loop flow where intermediate
  quality passes happen often but PRs open rarely. When the user is
  ready to PR, they invoke `commit-and-push` as usual; running both
  back-to-back is fine because `simplify` is idempotent.
---

# polish-checkpoint

A mid-session quality pass for the Cursor flow. Same pre-commit
checks as `commit-and-push`, minus everything past the simplify
step. Lets the human iterate with confidence that the working tree
is clean and the build is green, without locking the work into a PR.

## Why this exists

The fleet workflow bundles polish + commit + push + PR open into one
end-to-end skill (`commit-and-push`). That fits an autonomous worker
shipping a finished slice, but it doesn't fit an interactive Cursor
session where the human wants intermediate confidence checks while
still iterating. The Cursor flow needs a way to say "clean the diff
and verify the build, but don't commit yet" — that's what this skill
is. When the human is genuinely ready to PR, `commit-and-push` runs
the same pre-commit checks again (idempotent) and then does the git
work.

## When to invoke

Trigger when the user says:

- "checkpoint" / "polish for now" / "let's checkpoint"
- "verify what I have" / "is the working tree clean?"
- "clean up but don't commit" / "self-review without shipping"
- "run simplify and the build" / "polish + verify"

Do **not** auto-invoke. Always wait for an explicit cue. This is the
Cursor-flow analog of `simplify` plus a build verify; it's not a
save-on-edit hook.

This skill is **not** a substitute for `commit-and-push`. When the
user is ready to open a PR, they'll say "commit", "ship it", "open a
PR", or similar, and `commit-and-push` runs the same checks plus the
actual git work. Running this skill and then `commit-and-push`
back-to-back is fine — `simplify` is idempotent and a clean working
tree just means the commit-and-push pre-checks find nothing to do.

## Preconditions

1. **The working tree must have something to polish.** If `git
   status` is clean and there are no unpushed commits, report
   "nothing to check" and exit.
2. **Either branch is fine.** Cursor flow defaults to working off
   `master` with a dirty tree (the auto-branch happens at commit
   time); a feature branch also works. Don't switch branches as
   part of this skill.

## Flow

### 1. Gather state

```bash
git rev-parse --abbrev-ref HEAD
git status
git diff --stat && git diff
```

Group touched files by module (`engine/render/`, `engine/system/`,
`engine/prefabs/irreden/`, `creations/<name>/`, etc.). Read the most
specific `CLAUDE.md` for any module the diff touches before
inspecting it — module-specific rules override the engine baseline.

### 2. Run `simplify`

```
Skill: simplify
```

`simplify` reads the same diff and applies inline fixes for the usual
Irreden-Engine smells: per-entity `getComponent` inside system tick
functions, naming-convention slips (`m_` on public, missing `C_`
prefix on a new component), debug `std::cout` lines left from
troubleshooting, tautological doc comments, change-narration
comments, allocation in hot loops, etc. It auto-fixes the safe ones
and reports anything that needs human judgment.

After `simplify` finishes, re-check `git status` so you know the
post-simplify state. If `simplify` reverted everything (rare — only
happens when the only diffs were ones it cleaned up entirely),
report "working tree is clean after simplify" and stop.

### 3. Format

Run the formatter so the diff shape stays consistent across the
session:

```bash
fleet-build --target format
```

If `format` rewrote files, those edits are part of the polish — keep
them. Re-check `git status` afterwards so the report below reflects
the post-format state.

### 4. Verify the build (code diffs only)

If the diff is purely docs/markdown (no `.cpp`, `.hpp`, `.glsl`,
`.metal`, `CMakeLists.txt`, etc.), skip this step.

Otherwise build the target most directly affected by the diff:

```bash
fleet-build --target <touched-target>
```

Choosing the target:

- A creation diff — build that creation's executable target
  (`IRShapeDebug`, `IRCreationDefault`, `IRGame`, etc.).
- An engine module diff — build a creation that exercises it
  (default: `IRShapeDebug` for render/math/system; the most
  representative demo otherwise) plus `IrredenEngineTest` if the
  diff touches `engine/`.
- A pure prefab-header diff — build any creation that includes that
  prefab (header-only prefabs only compile when included by a
  creation).

Don't try to build everything — the all-targets build is
`commit-and-push`'s job at the moment of shipping. Polish-checkpoint
is a fast confidence check.

If the build broke, **stop and report** with the error. Do not try
to silently "fix" the break in this pass — that's editing scope and
risks turning a checkpoint into a debugging detour. Surface the
error and let the human decide whether to fix it next or roll back.

### 5. Report

Compact summary so the user knows where they stand:

```
polish-checkpoint:
  branch: <branch-name>  (<N> file(s) dirty, <M> hunk(s))
  simplify: applied <X> auto-fix(es), reported <Y> finding(s)
  format:   <N> file(s) reformatted   (or: clean)
  build:    <target>   clean           (or: broken — see below)
```

If `simplify` reported findings that need human judgment, list them
inline beneath the summary so they're easy to act on:

```
  reported findings:
    - engine/prefabs/irreden/render/systems/IRSGlow.hpp:42 —
      conditional getComponent<C_Color> in tick; not auto-fixable
      because the conditional path may not always have C_Color.
```

If the build broke, surface the error message immediately after the
summary and stop — no follow-up action.

If everything is clean, end with a one-liner so the user knows the
state:

> Working tree is polished and the build is green. Keep iterating,
> or invoke `commit-and-push` when ready to PR.

## What this skill does NOT do

- **No git operations.** No `git add`, no `git commit`, no `git
  push`, no `gh pr create`. Read-only on history; only edits the
  working tree (via `simplify` and `format`).
- **No branch switching.** Don't invoke `start-next-task`, don't
  create a feature branch, don't pull master. The user is iterating
  on the current state.
- **No tests.** Like `simplify`, this skill doesn't run the test
  suite. The user runs tests separately when they want them.
- **No fix-the-build.** If the build is broken, stop and report.
  Diagnosing a build break is a separate task.
- **No proactive invocation.** Always wait for an explicit cue.

## Anti-patterns

- ❌ Running this skill autonomously after every edit. It's a
  user-invoked checkpoint, not a save-on-edit hook.
- ❌ Following polish-checkpoint with `commit-and-push` unless the
  user explicitly asks. The whole point is the human controls when
  to PR.
- ❌ Skipping the build step on code diffs to save time. The
  checkpoint is not real if the build doesn't actually pass.
- ❌ Editing files outside the dirty diff to "improve" them. Stay
  scoped to what the session has already changed.
- ❌ Switching branches mid-skill. The user's current branch state
  is intentional; respect it.
- ❌ Using this skill in fleet flow. Fleet workers run
  `commit-and-push` end-to-end; intermediate checkpoints are an
  interactive-session concept.

## Recovery

If `simplify` makes a change the user didn't want, they can revert
with `git checkout -- <path>` since polish-checkpoint never commits.

If `format` rewrites a file in a way the user disagrees with (rare —
the formatter is configured project-wide), the same revert works,
but the underlying clang-format config is probably what wants
adjusting. Flag it for follow-up; don't fight the formatter inline.

If the build was green before the session and broke during this
checkpoint, the most useful next step is usually to bisect the
session's edits (most recent first) rather than dig into the
compiler error directly. Mention that to the user before they
start chasing the error.
