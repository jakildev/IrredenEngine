# AUTHOR-PIPELINE.md — shared build → verify → optimize → ship pipeline

The work-and-ship steps every author-side iteration (`role-worker.md`,
any class) runs between "branch is ready" and "PR is finalized"; class
deltas are tagged **[opus+ classes]** / **[sonnet class]**. Runtime
ceremonies: [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md); claim / PR-open /
stacked sequences: [`FLEET.md`](FLEET.md).

---

## Build and run

```
fleet-build --target <name>
```

If the diff adds files under `engine/prefabs/**/systems/` or changes the
signature of a free function or method used elsewhere, also build
`IrredenEngineTest` (or the engine library): creation targets link only
what they reference, so a demo-only build misses a new system's
`SystemName` enum entry and stale call sites under `test/**`. Run whatever
the touched code has an executable for: `--auto-screenshot` demos with
`fleet-run <name> --auto-screenshot 10` (no `--timeout`; the run ends at
`closeWindow()`), other GUI and test executables with `fleet-run --timeout
15 <name>`. Never `cd <dir> && ./<exe>` (compound-command security gate).
Verdict per [`FLEET.md § Clean-exit policy`](FLEET.md).

---

## Verify visual output (when it changed)

`git diff --name-only origin/master...HEAD` touching `engine/render/`,
`engine/prefabs/irreden/render/`, any `*.glsl` / `*.metal`, or
`creations/demos/*/src/**` / `creations/demos/*/main*.cpp` requires
**both** skills before `optimize` and `commit-and-push`, so fixes land in
the same commit:

a. **`attach-screenshots`** — before/after pairs (master vs working tree)
   under `docs/pr-screenshots/<branch>/`, embedded in the PR body; skip if
   that directory already has this branch's screenshots.
b. **`render-debug-loop`** — drives an `--auto-screenshot` creation, reads
   each frame, diagnoses against the topic-indexed reference; required by
   `engine/render/CLAUDE.md` "Verifying render changes" for shaders,
   render systems, or pipeline ordering.

Skip both only for docs, tests, mechanical refactors, or build/CI changes
with no visual effect (`engine/render/CLAUDE.md`'s exceptions list is
authoritative). "The default shot table is byte-identical for this
change" is a reason to change the shot, never to attach nothing: the
camera-yaw / pivot family is byte-identical at cardinal poses by design,
so use a non-cardinal shot (`attach-screenshots` § "Camera-yaw fixes need
a non-cardinal shot") or the PR's own sweep / verify harness as the
capture vehicle. **[sonnet class]** If `render-debug-loop` surfaces
something the diagnostic table does not match, or the fix would touch
core render pipeline code, stop and escalate one class up
(`role-worker.md` step 8a).

---

## Optimize before commit

Run `optimize`. **[opus+ classes]** almost always (skip only pure docs or
refactors that preserve hot-path structure). **[sonnet class]** only when
the change touches a system tick, a render stage, a shader, audio/video,
math hot paths, or the per-frame critical path. It runs before
`commit-and-push`'s `simplify` pass because it may add `IR_PROFILE_*`
blocks and rationale comments simplify should leave alone; re-run it
after feedback edits that change the perf surface.

---

## Acceptance evidence

The ticket-derived exit gate: the plan's `### Acceptance criteria`
(authored under [`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)'s
positive-fire rule) re-checked against the finished tree. Skip only when
there is no originating issue or the issue has neither a `## Plan` comment
nor an `**Acceptance criteria**` block.

1. Re-read the criteria: the newest `## Plan` comment's `### Acceptance
   criteria` as amended by later `## Plan corrections` (`fleet-issue view
   <N>`), or the body block for a no-plan issue.
2. Run each named check now, on the final tree, and record the exact
   command and the output line that proves the criterion fired (a count
   > 0, an asserted probe reading, a visible delta) — "nothing broke" is
   the build's job, not evidence. A figure or claim you derived yourself
   carries its method: the scoring convention behind a number, the
   executed sweep and its coverage behind a claim of absence.
3. Paste an `## Acceptance evidence` section into the PR body (template:
   `commit-and-push` `procedures/pr-body.md`), one row per criterion —
   criterion | check run | observed.
4. Grade as written. *Unverifiable on this host* → `unverifiable on
   <host>: <reason>` in the row (cite a fleet-tracked host limitation's
   issue and re-check it is still open at rebase); the reviewer and smoke
   lane pick it up. *Fails* → not done: fix it, or escalate per
   `role-worker.md` step 8 if the criterion's premise is false. *Satisfied
   by a different mechanism* → record what proves it and the delta from
   the plan; a criterion whose literal wording the diff violates is
   *Fails*, not "met in substance".

If `commit-and-push`'s simplify pass applies a behavior-affecting fix,
re-run the affected checks before the PR opens; the reviewer grades this
table against the plan.

---

## Finalize the PR

`commit-and-push` pushes to the existing PR branch (cwd's repo). Then drop
the WIP label, strip a trailing `[WIP]` from the title (a squash-merge
writes the title into master's history), and release the claim.
Unverifiable on this host is never a reason to leave `fleet:wip` on: record
the host limitation in the acceptance evidence row, then remove the label so
the PR is reviewable and the cross-host smoke lane can pick it up:

```
# engine task
gh pr edit <N> --remove-label "fleet:wip"
title="$(gh pr view <N> --json title -q .title)"
stripped_title="$(sed -E 's/[[:space:]]*\[WIP\][[:space:]]*$//' <<< "$title")"
[[ -n "$stripped_title" && "$stripped_title" != "$title" ]] && gh pr edit <N> --title "$stripped_title"
fleet-claim release <issue-#>
```

Game task (you `cd`'d into the game worktree at claim time): add `--repo
jakildev/irreden` to each `gh` call and use `fleet-claim --repo game
release <issue-#>`. Paste the PR URL.

`fleet-claim release` clears the FS lock and the worktree reservation.
The issue's `fleet:claim-<host>-<agent>` and `fleet:in-progress` stay
while a live PR backs the claim (never hand-strip them); with no live PR
(a design-blocked park, a decline after claim) `release` clears both
itself unless another host's claim is still live. `fleet-claim cleanup
--gh` is the TTL safety net for a claim never released. Then the shared
shutdown —
[`FLEET-RUNTIME.md § Per-iteration shutdown`](FLEET-RUNTIME.md#per-iteration-shutdown--final-step).
