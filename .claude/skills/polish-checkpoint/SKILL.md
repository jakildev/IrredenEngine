---
name: polish-checkpoint
description: >-
  Runs a mid-session quality checkpoint without committing — simplifies
  the dirty tree and builds the touched target, then stops before any git
  operation (no staging, commit, push, or PR). Use when the user says
  "checkpoint", "polish for now", "verify what I have", "clean up but
  don't commit", "self-review without shipping", or "polish + verify";
  never proactively. `simplify` is idempotent, so `commit-and-push` can
  follow immediately.
---

# polish-checkpoint

The pre-commit half of `commit-and-push` for the Cursor / human-in-the-loop
flow. Either branch is fine — cursor flow works off `master` with a dirty
tree until commit time — and the skill never switches branches.

If `git status` is clean and there are no unpushed commits, report
"nothing to check" and exit.

1. `git rev-parse --abbrev-ref HEAD`, `git status`, `git diff --stat`,
   `git diff`. Read the most specific `CLAUDE.md` for each module the diff
   touches.
2. `Skill: simplify`. Re-check `git status` afterwards; if simplify left
   the tree clean, report that and stop.
3. Build (code diffs only — a docs/markdown-only diff skips this):
   ```bash
   fleet-build --target <touched-target>
   ```
   A creation diff builds that creation's target; an engine module diff
   builds a creation that exercises it (`IRShapeDebug` for
   render/math/system, the most representative demo otherwise) plus
   `IrredenEngineTest` when `engine/` changed; a header-only prefab diff
   builds any creation that includes it. Not the all-targets build — that
   is `commit-and-push`'s job. A broken build is reported, not fixed, in
   this pass; bisecting the session's edits (most recent first) is usually
   faster than reading the compiler error.
4. Report:
   ```
   polish-checkpoint:
     branch: <branch-name>  (<N> file(s) dirty, <M> hunk(s))
     simplify: applied <X> auto-fix(es), reported <Y> finding(s)
     build:    <target>   clean           (or: broken — see below)
     reported findings:
       - <path:line> — <finding>
   ```
   Clean: "Working tree is polished and the build is green. Keep
   iterating, or invoke `commit-and-push` when ready to PR."

No git operations, no branch switching, no tests, no build fixes, no
following up with `commit-and-push` unless asked. Any unwanted simplify
edit reverts with `git checkout -- <path>`.
