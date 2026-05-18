---
name: polish-checkpoint
description: >-
  Run a mid-session quality checkpoint without committing. Mirrors the
  pre-commit phase of `commit-and-push` (simplify the dirty tree,
  verify the build for the touched target) but stops before
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

## When to invoke

Trigger when the user says:

- "checkpoint" / "polish for now" / "let's checkpoint"
- "verify what I have" / "is the working tree clean?"
- "clean up but don't commit" / "self-review without shipping"
- "run simplify and the build" / "polish + verify"

Do **not** auto-invoke. Always wait for an explicit cue.

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
Irreden-Engine smells — ECS checks per [`.claude/rules/cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md),
naming-convention slips, debug `std::cout` lines, tautological doc comments,
and change-narration comments. It auto-fixes the safe ones and reports anything
that needs human judgment.

After `simplify` finishes, re-check `git status` so you know the
post-simplify state. If `simplify` reverted everything (rare — only
happens when the only diffs were ones it cleaned up entirely),
report "working tree is clean after simplify" and stop.

### 3. Verify the build (code diffs only)

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

### 4. Report

Compact summary so the user knows where they stand:

```
polish-checkpoint:
  branch: <branch-name>  (<N> file(s) dirty, <M> hunk(s))
  simplify: applied <X> auto-fix(es), reported <Y> finding(s)
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
  working tree (via `simplify`).
- **No branch switching.** Don't invoke `start-next-task`, don't
  create a feature branch, don't pull master. The user is iterating
  on the current state.
- **No tests.** Like `simplify`, this skill doesn't run the test
  suite. The user runs tests separately when they want them.
- **No fix-the-build.** If the build is broken, stop and report.
  Diagnosing a build break is a separate task.
- **No proactive invocation.** Always wait for an explicit cue.

## Anti-patterns

- Running this skill autonomously after every edit. It's a
  user-invoked checkpoint, not a save-on-edit hook.
- Following polish-checkpoint with `commit-and-push` unless the
  user explicitly asks. The whole point is the human controls when
  to PR.
- Skipping the build step on code diffs to save time. The
  checkpoint is not real if the build doesn't actually pass.
- Editing files outside the dirty diff to "improve" them. Stay
  scoped to what the session has already changed.
- Switching branches mid-skill. The user's current branch state
  is intentional; respect it.
- Using this skill in fleet flow. Fleet workers run
  `commit-and-push` end-to-end; intermediate checkpoints are an
  interactive-session concept.

## Recovery

If `simplify` makes a change the user didn't want, they can revert
with `git checkout -- <path>` since polish-checkpoint never commits.

If the build was green before the session and broke during this
checkpoint, the most useful next step is usually to bisect the
session's edits (most recent first) rather than dig into the
compiler error directly. Mention that to the user before they
start chasing the error.
