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

**"The default shot table is byte-identical for this change" is a
reason to change the SHOT, never a reason to attach nothing.** The
whole camera-yaw / pivot family is byte-identical at cardinal poses by
design (see `.claude/skills/attach-screenshots/SKILL.md` §"Camera-yaw
fixes need a non-cardinal shot"), so every PR in it hits this decision
point. When the PR ships or exercises its own non-cardinal harness (a
sweep/verify mode like `--pivot-verify`), that harness IS the capture
vehicle for the required before/after pair — one already-built binary,
one run (#2650; #2585 shipped zero screenshots on a correct-but-
incomplete "byte-identical by construction" rationale and ate a full
review round).

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

## Acceptance evidence

The ticket-derived exit gate. Every step above is keyed off *which
files changed*. This step is keyed off the *ticket*: it closes the
loop between the plan's `### Acceptance criteria` and the PR that
claims to satisfy them. Criteria are authored at plan time under
[`PLANNING-PROTOCOL.md`](PLANNING-PROTOCOL.md)'s positive-fire rule
(step 2 of its flow); this is where they get re-checked against the
finished tree instead of silently trusted.

Skip only when the work has no originating issue (`Issue:` field
`(none)`) or the issue has no `## Plan` comment / plan file — then
there are no authored criteria to grade.

1. **Re-read the plan's `### Acceptance criteria`** — from
   `.fleet/plans/issue-<N>.md` if the branch carries it, else the
   issue's `## Plan` comment (`gh issue view <N> --comments`).
2. **Run each named check NOW, on the final tree.** Evidence from an
   earlier iteration is stale if the tree changed since. Record the
   exact command and the observed output line that proves the criterion
   *fired* — a count > 0, an asserted probe reading, a visible delta.
   "Nothing broke" is not evidence; that's what the build/run steps
   already established.
3. **Paste the results into the PR body** as an
   `## Acceptance evidence` section (template:
   `commit-and-push` `procedures/pr-body.md`) — one row per criterion:
   criterion | check run | observed.
4. **Grade honestly.** Three non-met shapes, each with its own move:
   - *Unverifiable on this host* (needs the other backend, a GL host,
     a game build): record `unverifiable on <host>: <reason>` in the
     row — never silently drop it. The reviewer and the cross-host
     smoke lane pick it up from there.
   - *Fails*: the task is not done. Fix it — or, if the criterion
     itself turned out to be wrong (plan premise falsified), escalate
     per `role-worker.md` step 8 instead of shipping around it.
   - *Satisfied by a different mechanism than planned*: record what
     actually proves it and note the delta from the plan, so the
     reviewer isn't grading against a stale approach.

If the `commit-and-push` simplify pass later applies a
behavior-affecting fix, re-run the affected checks before the PR
opens — the table must describe the tree that ships.

The reviewer side audits this table against the plan; a missing or
hand-waved table costs a review round-trip.

---

## Finalize the PR

Use the `commit-and-push` skill to push your work commits to the
existing PR branch (commit-and-push uses cwd's git repo automatically).
Then remove the WIP label, strip any trailing `[WIP]` the claim-time
title picked up, and release the claim. The `[WIP]` marker is a worker
convention with no minting site to key on — only the label is load-bearing
for the fleet's own tooling — but a squash-merge writes the PR *title* into
master's commit history, so a stale trailing marker ships into permanent
history if left on the title (#2788):

```
# engine task
title="$(gh pr view <N> --json title -q .title)"
stripped_title="$(sed -E 's/[[:space:]]*\[WIP\][[:space:]]*$//' <<< "$title")"
[[ -n "$stripped_title" ]] && gh pr edit <N> --remove-label "fleet:wip" --title "$stripped_title"
fleet-claim release <issue-#>
```

**Game task** — you `cd`'d into the game worktree at
claim time, so `commit-and-push` already targets the right repo; add
`--repo jakildev/irreden` to `gh` and `--repo game` to `fleet-claim`
so the right PR + the right slug are targeted:

```
# game task
title="$(gh pr view <N> --repo jakildev/irreden --json title -q .title)"
stripped_title="$(sed -E 's/[[:space:]]*\[WIP\][[:space:]]*$//' <<< "$title")"
[[ -n "$stripped_title" ]] && gh pr edit <N> --repo jakildev/irreden --remove-label "fleet:wip" --title "$stripped_title"
fleet-claim --repo game release <issue-#>
```

Paste the PR URL.

> **Claim-label lifecycle.** `fleet-claim release` clears the local
> filesystem lock and the worktree reservation. The issue's
> `fleet:claim-<host>-<agent>` and `fleet:in-progress` labels are
> **left in place while a live PR backs the claim** — they persist
> through that PR's review/merge lifecycle. Don't hand-strip them.
> When no live PR backs the claim, `release` clears both labels
> itself so the scout stops treating the issue as in-progress and any
> worker can re-claim. Two such cases, and you do nothing special for
> either — the `fleet-claim release <N>` you already run handles it:
>
> - **design-blocked / design-unblocked escalation** — the matching PR
>   is parked, awaiting the architect (#1488).
> - **decline after claim** — you claimed, found the task unworkable,
>   commented why, and released without ever opening a PR (#2732).
>   Retaining the labels here parked a still-`fleet:queued` task in
>   the scout's `in_progress[]` bucket, invisible to every pane's
>   queue walk, until the `cleanup --gh` TTL aged it out.
>
> `fleet-claim cleanup --gh` remains the TTL safety net for a claim
> abandoned with no `release` call at all (crash, killed pane).
>
> One issue can carry claim labels from **two hosts**. Every path that
> clears `fleet:in-progress` — `release`, the `cleanup --gh` TTL sweep,
> and the `reset-sweep-host-claims` boot sweep — first checks whether a
> claim it is *not* retiring is still live, and keeps the label if so.
> Your host's claim going dead says nothing about the other host's, and
> clearing `fleet:in-progress` under a live claim advertises an owned
> task as free.

Then run the shared shutdown ceremony — see
[`FLEET-RUNTIME.md § Per-iteration shutdown`](FLEET-RUNTIME.md#per-iteration-shutdown--final-step).
