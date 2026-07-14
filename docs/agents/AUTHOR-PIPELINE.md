# AUTHOR-PIPELINE.md — shared build → verify → optimize → ship pipeline

The execution pipeline shared by every author-side iteration
(`role-worker.md`, any class). Once a task is claimed and the plan is
read, all classes run the same build → run → verify-visual → optimize
→ finalize sequence. The role file points here rather than restating
it, so classes cannot drift on shared mechanics.

Class-specific deltas (opus+-vs-sonnet class, engine-vs-game) are
called out inline below with **[opus+ classes]** / **[sonnet class]**
tags. Where no tag appears, the step is identical for all classes.

The per-iteration runtime ceremonies (heartbeat, reservation check,
shutdown) live in [`FLEET-RUNTIME.md`](FLEET-RUNTIME.md); the
claim / PR-open / stacked-PR command sequences live in
[`FLEET.md`](FLEET.md). This doc covers only the work-and-ship steps
between "branch is ready" and "PR is finalized."

---

## Build and run

```
fleet-build --target <name>
```

**If the diff adds files under `engine/prefabs/**/systems/` — or changes
the signature of any free function or method consumed elsewhere in the
tree** — also build `IrredenEngineTest` (or the engine static library)
before the PR claims its test plan is green. Creation targets like
`IRShapeDebug` only compile and link what they reference, so a demo-only
build is blind both to a new system's missing `SystemName` enum entry
(silent linker error) and to stale call sites of a changed signature that
live only in `test/**` (#2313: nine call sites in
`test/render/per_canvas_light_scope_test.cpp` broke the suite build while
the PR's demo targets built green).

If the touched code has an executable target, run it to confirm it
launches cleanly:

- **Demos that support `--auto-screenshot`:** `fleet-run <name> --auto-screenshot 10` (no `--timeout` — auto-screenshot fires `closeWindow()` when done).
- **All other GUI executables:** `fleet-run --timeout 15 <name>` — 5 seconds is too short for a demo mid-init.
- **Test executables:** `fleet-run --timeout 15 <name>` as a safety net.

**Never** use `cd <dir> && ./<exe>` — that triggers the
compound-command security gate. Untested commits are the single
biggest waste of reviewer-agent time.

---

## Verify visual output (when it changed)

Check whether the diff touches visual/render code:

```
git diff --name-only origin/master...HEAD
```

The trigger file set:

- `engine/render/` (any file)
- `engine/prefabs/irreden/render/` (any file)
- Any `*.glsl` or `*.metal` shader file anywhere in the tree
- `creations/demos/*/src/**` or `creations/demos/*/main*.cpp`

When the diff includes any of those, you must invoke **BOTH** skills:

a. **`attach-screenshots`** — captures before/after pairs (master vs
   working tree) and writes them under `docs/pr-screenshots/<branch>/`
   so the PR body can embed them via raw GitHub URLs. Does not
   diagnose — see (b). Skip if `docs/pr-screenshots/<branch>/` already
   contains screenshots from a prior run on this branch.

b. **`render-debug-loop`** — drives any creation that supports
   `--auto-screenshot` (today: `shape_debug`), reads each captured
   frame, and diagnoses rendering issues against the topic-indexed
   reference (trixel/SDF shapes, lighting, backend-parity symptoms).
   Catches visual regressions that would otherwise reach the reviewer
   (or, worse, ship). Required by `engine/render/CLAUDE.md` "Verifying
   render changes" for any PR touching shaders, render systems, or
   pipeline ordering.

The two skills serve different purposes — `attach-screenshots`
produces the PR record; `render-debug-loop` is the diagnostic pass
that confirms the change actually renders correctly. Run both; do not
substitute one for the other.

Skip BOTH if the diff is purely docs, tests, mechanical refactors
(rename, extract-header, add-logging), or build/CI changes with no
visual effect. The exceptions list in `engine/render/CLAUDE.md`
"Verifying render changes" is authoritative — when in doubt, run the
loop; a missing diagnostic pass is a fast reviewer-rejection.

Both must complete before `optimize` and `commit-and-push` so any
resulting fixes land in the same commit as the code change.

**[sonnet class]** If `render-debug-loop` surfaces something subtler
than expected (the diagnostic table doesn't match a known symptom, or
the fix would touch core render pipeline code), STOP and escalate (re-tag
the task one class up per `role-worker.md` step 8a and release). That's
an opus-tier debugging session, not a sonnet one.

---

## Optimize before commit

Run the `optimize` skill. It profiles the new code, identifies
hotspots, and verifies no regressions.

**When to run:**

- **[opus+ classes]** Almost always — heavy-class work almost always
  touches perf-critical code (engine/render, engine/system,
  engine/world, engine/audio, engine/video, engine/math). Skip only
  for pure docs or mechanical refactors that preserve hot-path
  structure.
- **[sonnet class]** Only if the change touches a system tick, a
  render pipeline stage, a shader, audio/video, math hot paths, or
  anywhere on the per-frame critical path. Skip for pure docs, tests,
  mechanical refactors, or build/CI changes.

You don't need to invoke `simplify` separately — `commit-and-push`
runs it as part of its flow. Running `optimize` first matters because
optimize may add `IR_PROFILE_*` blocks and rationale comments that
simplify should leave alone; commit-and-push's simplify pass then
polishes everything together.

The same applies when **addressing review feedback** — after editing
in response to comments, re-run `optimize` (if the perf surface
changed) before invoking `commit-and-push` to push the fix.

---

## Finalize the PR

Use the `commit-and-push` skill to push your work commits to the
existing PR branch (commit-and-push uses cwd's git repo automatically).
Then remove the WIP label and release the claim.

```
# engine task
gh pr edit <N> --remove-label "fleet:wip"
fleet-claim release <issue-#>
```

**Game task** — you `cd`'d into the game worktree at
claim time, so `commit-and-push` already targets the right repo; add
`--repo jakildev/irreden` to `gh` and `--repo game` to `fleet-claim`
so the right PR + the right slug are targeted:

```
# game task
gh pr edit <N> --repo jakildev/irreden --remove-label "fleet:wip"
fleet-claim --repo game release <issue-#>
```

Paste the PR URL.

> **Claim-label lifecycle.** `fleet-claim release` clears the local
> filesystem lock and the worktree reservation. The issue's
> `fleet:claim-<host>-<agent>` and `fleet:in-progress` labels are
> normally **left in place** — they persist through the PR's
> review/merge lifecycle and are swept by `fleet-claim cleanup --gh`
> only when the issue closes or the claim goes stale with no matching
> open `claude/<N>-*` PR. Don't hand-strip them. The one exception is
> a **design-blocked / design-unblocked** escalation: when the issue's
> matching PR is parked, `release` clears those two labels itself so
> the scout stops treating the issue as in-progress and any worker can
> re-claim once the architect unblocks it (#1488). You don't do
> anything special — the design-escalation step's existing
> `fleet-claim release <N>` call handles it.

Then run the shared shutdown ceremony — see
[`FLEET-RUNTIME.md § Per-iteration shutdown`](FLEET-RUNTIME.md#per-iteration-shutdown--final-step).
