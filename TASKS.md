# TASKS

Shared task queue for parallel agents. Both human and agent maintainers
append here, and the next unblocked item is what an idle agent should pick up.

## How to use this file

1. **Picking a task:** skim the `## Open` section. Find the first `[ ]` item
   whose **Owner** is `free` or your worktree name, and whose **Blocked by**
   list is empty. **Then cross-check `gh pr list --state open`** — if any
   open PR's title or branch name looks like it's already working on that
   task, skip to the next candidate. The open-PR list is the authoritative
   claim signal; the `[~]` flip on a feature branch is invisible to other
   agents until merge, so two agents can race to claim the same task in the
   ~minutes-to-hours window between picking and merging. Cross-checking
   `gh pr list` closes most of that race.

   Once you've picked, change the status to `[~]` (in progress), set Owner
   to your worktree, and push the edit in your first commit so other agents
   see it once your PR merges.
2. **Finishing a task:** change `[~]` to `[x]`, set the final commit or PR
   URL in the **Links** line, and move the item to `## Done — last 20` at
   the bottom. Keep only the last 20 done items; prune older ones.
3. **Adding a task:** append to `## Open` with the template below. Err on the
   side of creating small tasks (one PR's worth of work). If a task needs
   research first, file it as `Research:` — the deliverable is a short
   findings note, not code. The fastest way to add a task is to ask the
   `queue-manager` pane in the fleet — paste a rough description and it
   will categorize, tag, format, and file the queue-update PR for you.
4. **Blocking on another task:** put the blocking task's title in
   **Blocked by**. An agent should skip blocked items. For cross-repo
   blocks (game blocked on engine), put the engine PR URL in **Blocked by**
   so any agent can resolve it without context.
5. **Touching this file:** always stage and commit `TASKS.md` edits in the
   same PR as the work they describe, so history stays consistent.
   Queue-maintenance-only PRs (e.g. `queue: add task X`, batched task
   adds) are also explicitly allowed and merge fast.

### Race conditions and how the fleet handles them

`TASKS.md` is git-versioned, which means an agent's `[~]` claim only
becomes visible to other agents after its PR merges. Between picking and
merging, two agents can independently pick the same task. The fleet
defends against this in three layers:

1. **Pre-pick `gh pr list` cross-check** (rule 1 above) — closes most
   of the window.
2. **Merge conflict on the second `[~]` flip** — both PRs edit the same
   line in `TASKS.md`, so whichever one merges second will hit a
   GitHub-side merge conflict and refuse to auto-merge. The human
   reviewer sees the conflict before merging and rejects the loser.
3. **Loser requeues and picks again** — the agent whose PR conflicts
   uses `start-next-task` to reset to a fresh branch off `origin/master`,
   picks the next available task, and moves on. The work isn't lost; it
   just gets rescheduled.

The local `fleet-claim` script adds a fourth layer: agents call
`fleet-claim claim "T-NNN" <agent>` using the task's **ID** (not the
free-text title) before starting work. The short deterministic ID
prevents the failure mode where two agents slugify different
paraphrasings of the same title and both succeed. If `fleet-claim`
returns exit 1 (already taken), skip to the next task.

This file is the **engine-level** task queue. Private creations that live
under `creations/` may define their own `TASKS.md` inside their own
directory — those are tracked independently and should not be mixed here.
Do not queue game or creation-specific gameplay tasks in this file; queue
them in the creation's own `TASKS.md`.

## Task template

```
- [ ] **<short title>** — <one-line goal>
  - **ID:** T-NNN  (sequential, assigned by the queue-manager)
  - **Area:** engine/render | engine/entity | engine/prefabs/... | docs | build | creations/demos/... | ...
  - **Model:** opus | sonnet  (which model should run this)
  - **Owner:** free | <worktree-name>
  - **Blocked by:** (none) | <title of blocking task>
  - **Stack:** T-XXX..T-YYY <slug>  (optional — only for tasks in a stacked chain sharing a parent epic; omit for standalone tasks)
  - **Acceptance:** <concrete check: build passes, test X passes, PR merged, screenshot Y looks like Z>
  - **Issue:** (none) | #N  (GitHub issue number, if task originated from an issue)
  - **Notes:** <context, links, prior attempts>
  - **Links:** (fill in PR URL when done)
```

The **ID** is the canonical claim key. When calling `fleet-claim`, pass the
task ID (e.g. `fleet-claim claim "T-003" sonnet-fleet-1`), **not** the
free-text title. IDs are short and unambiguous — agents can't accidentally
paraphrase them, which is the failure mode that free-text title slugification
is vulnerable to.

The **Stack** field groups child tasks of a shared parent epic so a
human can follow the chain across `## Open`. Format:
`T-<min>..T-<max> <slug>`; slug is a kebab-case identifier shared by
all siblings. Informational only — `fleet-claim` and the scout cache
ignore it. Standalone tasks omit the field entirely. The queue-manager
populates it during ingestion when a child issue declares membership;
see `role-queue-manager.md` for the detection rule.

Status markers: `[ ]` open, `[~]` in progress, `[x]` done, `[!]` blocked/stuck.

### Model tagging (important)

Tag every task with the intended model. Default assumption:

- `[opus]` — anything touching `engine/render/`, `engine/entity/`,
  `engine/system/`, `engine/world/`, `engine/audio/`, `engine/video/`,
  `engine/math/` (non-trivial), or ECS/render optimization, or concurrency,
  or ownership/lifetime rules. Also final review on anything important.
- `[sonnet]` — test generation, doc passes, mechanical refactors with a
  clear spec, first-pass code review, clearly-scoped items already thought
  through, anything in `creations/demos/`, small bounded shader tweaks.

A Sonnet agent that picks up an `[opus]` task should escalate instead of
charging ahead. A Sonnet agent that finds a `[sonnet]` task is subtler
than expected (touches an invariant, a lifetime, a race) should stop and
requeue with `[opus]`. The top-level `CLAUDE.md` has the full split.

## Good tasks to queue here (engine-only)

Small and bounded is the target. Good shapes for this queue:

- **Test generation** — "write exhaustive tests for `engine/math/physics.hpp`
  ballistic helpers"
- **Docs / API reference** — "document every `IRRender::` free function in
  `engine/render/CLAUDE.md`"
- **Benchmark / profiling report** — "profile `IRShapeDebug` at zoom 4 with
  N voxels and file a report"
- **Isolated refactor** — "port `engine/common/ir_constants.hpp` to constexpr"
- **Build / CI hardening** — "add a `format-check` CI target that fails on
  stale clang-format output"
- **FFmpeg / audio interface hardening** — "add bounds checks to
  `VideoRecorder::submitVideoFrame` stride handling"
- **Compile-time cleanup** — "reduce `engine/render/` TU rebuild cascade by
  moving X out of the low header"
- **Shader hygiene** — "extract repeated iso-projection math in
  `engine/render/src/shaders/` into `ir_iso_common.glsl`"

Avoid:

- Tasks that touch core ECS types (`engine/entity/`) — do those by hand.
- "Refactor the render loop" — too broad, no single PR scope.
- Anything that would require changing the public `ir_*.hpp` surface across
  multiple modules in one PR.
- Gameplay or content work for any specific creation — belongs in that
  creation's own task queue.

---

## Open

<!-- Add tasks below this line. -->

- [~] **CLAUDE.md sharing: shared docs for creations, with per-creation opt-out** — design and implement mechanism for creations to inherit engine CLAUDE.md sections by reference with opt-out support
  - **ID:** T-048
  - **Area:** docs, creations
  - **Model:** opus
  - **Owner:** claude/T-048-claude-md-sharing
  - **Blocked by:** (none)
  - **Acceptance:** (1) design doc answers: sharing mechanism (symlinks / include directives / build-merge), granularity (whole-file vs per-section), opt-out form, discovery model; (2) reference implementation: at least one creation uses the mechanism to inherit engine baseline docs, with one opt-out demonstrated; (3) audit pass produces clear list of "creation-shared" vs "engine-internal" CLAUDE.md sections; (4) documented in `engine/CLAUDE.md` and `creations/CLAUDE.md`
  - **Issue:** #299
  - **Notes:** Open design questions captured in issue body: mechanism, granularity, opt-out form, and discovery. Design doc must answer all four before implementation. The motivation is that "see engine root CLAUDE.md for X" soft references rot as engine docs evolve.
  - **Links:**

- [ ] **Modifier framework: core runtime (registry, 5 resolver systems, source sweep)** — implement FieldBindingId registry, C_ResolvedFields machinery, 5 resolver systems, pipeline helper, source-destruction sweep, applyToField query
  - **ID:** T-050
  - **Area:** engine/prefabs/irreden/common, engine/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-049..T-053 modifier-framework
  - **Acceptance:** (1) unit tests cover ADD/MULTIPLY/SET/CLAMP_MIN/CLAMP_MAX/OVERRIDE composition correctness, composition order pinned and tested, ticksRemaining decay exact, source-destruction sweep correct, global+exempt archetype routing correct, lambda escape hatch; (2) no `getComponent`/`getComponentOptional` calls inside any tick body; (3) resolver tick at 1000 entities × 5 modifiers < 0.5 ms, recorded in PR body; (4) builds clean on `linux-debug` AND `macos-debug`
  - **Issue:** #304
  - **Notes:** Child 2 of 5. Resolver pipeline order (end of UPDATE phase, before RENDER reads): ModifierDecay → GlobalModifierDecay → ModifierResolveGlobal → ModifierResolveExempt → ModifierResolveLambda. Use `beginTick` to capture global modifier vector pointer once per pipeline execution. See `.fleet/plans/T-050.md` for full locked design.
  - **Links:**

- [ ] **Modifier framework: migrate position + velocity-drag patterns** — reframe existing position-offset and velocity-drag hand-rolled patterns onto the framework; preserve behavior exactly
  - **ID:** T-051
  - **Area:** engine/prefabs/irreden/common, engine/prefabs/irreden/update, engine/world
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-050
  - **Stack:** T-049..T-053 modifier-framework
  - **Acceptance:** (1) all demos and creations consuming position-global or drag-modulated velocity render and behave identically before and after; (2) `fleet-run IRShapeDebug` no visible regressions; (3) `render-debug-loop` before/after screenshots in PR body; (4) builds clean on `linux-debug` AND `macos-debug`; (5) removed-line count > added-line count (framework absorbs the one-off pattern)
  - **Issue:** #305
  - **Notes:** Child 3 of 5. Two migrations: (a) position pattern — `C_Position3D` base + offset pushed as C_Modifiers entries → `C_PositionGlobal3D` as resolved; (b) velocity drag — `C_VelocityDrag` becomes MULTIPLY modifier on velocity field, `system_velocity_drag.hpp` shrinks. Worker chooses exact migration shape for each; behavior preservation is the hard gate. Deferred: color animation, spring color, spawn glow, texture scroll.
  - **Links:**

- [ ] **Modifier framework: Lua bindings** — expose field registration, modifier push/remove/query, and TransformKind enum to Lua via sol2
  - **ID:** T-052
  - **Area:** engine/script, engine/prefabs/irreden/common
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-050
  - **Stack:** T-049..T-053 modifier-framework
  - **Acceptance:** (1) Lua script registers field and pushes one modifier per transform kind (6 total), observing correct resolved values; (2) Lua script pushes lambda modifier and confirms it's applied; (3) Lua script pushes global modifier, tags one entity `C_NoGlobalModifiers`, confirms only non-exempt entities receive globals; (4) `removeBySource` removes only matching modifiers; (5) builds clean on active preset
  - **Issue:** #306
  - **Notes:** Child 4 of 5. API surface: `ir.modifier.registerField`, `push`, `pushGlobal`, `pushLambda`, `removeBySource`, `applyToField`; `TransformKind` enum as `ir.modifier.ADD/MULTIPLY/SET/CLAMP_MIN/CLAMP_MAX/OVERRIDE`. Bindings live on the modifier prefab's Lua surface — NOT `ir.render.*`. sol2 lambda lifetime for `pushLambda` is the trickiest binding — escalate if the `std::function` doesn't survive GC.
  - **Links:**

- [ ] **Modifier framework: modifier_demo creation (visual showcase)** — scaffold `modifier_demo` creation with 8 key-toggleable capabilities, on-screen HUD, and auto-screenshot shot list
  - **ID:** T-053
  - **Area:** creations/demos/modifier_demo, docs
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-050, T-051, T-052
  - **Stack:** T-049..T-053 modifier-framework
  - **Acceptance:** (1) `fleet-run IRModifierDemo` launches and shows a row of moving cubes; (2) each numbered key 1-8 triggers corresponding capability with obvious visual change; (3) on-screen HUD shows resolved values matching active modifiers (verified manually on ≥3 capabilities); (4) `fleet-run IRModifierDemo --auto-screenshot 12` produces committed shot list; (5) builds clean on `linux-debug` AND `macos-debug`
  - **Issue:** #307
  - **Notes:** Child 5 of 5. Use `create-creation` skill to scaffold — don't hand-roll CMakeLists.txt. 8 capabilities: Haste (MULTIPLY 1.5×), Stun (SET 0), Slow (MULTIPLY 0.3×), Stack (Haste+Slow composed), Global Slow (singleton, one exempt cube), Lambda Sinusoidal, Source Kill, Clamp (CLAMP_MAX 0.5 + Haste). All wiring in Lua via T-052 bindings. ~200-line Lua script target. Cross-link from `engine/prefabs/irreden/common/CLAUDE.md` when done.
  - **Links:**

- [ ] **Render: world Z-yaw view/camera transform foundation** — add `C_CameraYaw`, per-tick cardinal/residual split helper, thread both yaw values to GPU feeders; foundation for Z-yaw rotation epic
  - **ID:** T-054
  - **Area:** engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-054..T-057 z-yaw-pipeline
  - **Acceptance:** (1) `C_CameraYaw { float visualYaw_; }` compiles and registers in ECS; (2) per-tick helper derives `rasterYaw` = nearest 90° cardinal and `residualYaw` = `visualYaw - rasterYaw`; (3) both values threaded to GPU feeder UBOs for trixel and SDF shaders; (4) `IRPrefab::Camera::setYaw/getYaw` API present; (5) visual + perf parity at `visualYaw=0` verified via `render-debug-loop`; (6) audit note in PR body lists all `engine/render/` sites that bake-in iso-basis assumptions; (7) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #317
  - **Notes:** Foundation child of epic #310 (two-yaw architecture). `rasterYaw` feeds the trixel raster (T-055); `residualYaw` feeds the screen-space 2D residual composite pass (new unfiled child of #310); `visualYaw` feeds SDF (T-056) and picking (T-057). `visualYaw` is the only canonical value — gameplay never sees the cardinal split. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [ ] **Render: trixel rasterization under cardinal-snap Z-yaw** — update trixel raster shader to pick one of 4 basis-vector permutations from `rasterYaw`; GLSL + MSL parity
  - **ID:** T-055
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-054
  - **Stack:** T-054..T-057 z-yaw-pipeline
  - **Acceptance:** (1) shader selects the correct basis-vector permutation for each cardinal yaw (0°, 90°, 180°, 270°); (2) `render-debug-loop` visual parity at yaw=0; (3) correct rasterization at all four cardinal angles verified visually; (4) per-frame cost at any cardinal yaw ≤ cost at yaw=0 (no regression); (5) GLSL and MSL implementations at parity; (6) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #311
  - **Notes:** Child 2 of 5 of epic #310. Re-scoped from "arbitrary" to "cardinal-snap" — continuous visual rotation comes from the residual 2D composite pass (new unfiled sibling). `rasterYaw` provided by T-054. Does NOT handle fractional/sub-cardinal yaw — voxels always land on integer trixel pixels. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [ ] **Render: SDF shape rasterization under arbitrary Z-yaw** — update SDF compute shader to consume continuous `visualYaw`; GLSL + MSL parity
  - **ID:** T-056
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-054
  - **Stack:** T-054..T-057 z-yaw-pipeline
  - **Acceptance:** (1) SDF shader reads `visualYaw` (continuous radians, provided by T-054); (2) `render-debug-loop` visual parity at yaw=0; (3) correct SDF rendering at several non-cardinal yaw values verified visually; (4) GLSL and MSL implementations at parity; (5) `fleet-build --target IRShapeDebug` clean on `linux-debug` AND `macos-debug`
  - **Issue:** #312
  - **Notes:** Child 3 of 5 of epic #310. SDF is the "free half" — no integer-alignment requirement, so continuous-yaw works directly. Reads `visualYaw` (not `rasterYaw`). Runs in parallel with T-055 once T-054 lands. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

- [ ] **Render/input: screen-to-world picking under Z-yaw** — update picking inverse to compose `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹`; audit duplicate transform copies
  - **ID:** T-057
  - **Area:** engine/render, engine/input
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-054, render: screen-space 2D residual yaw composite pass (not yet queued — see epic #310)
  - **Stack:** T-054..T-057 z-yaw-pipeline
  - **Acceptance:** (1) picking inverse composes `R2D(-residualYaw)` then `R(-rasterYaw)·M⁻¹` per plan; (2) correct world coords at yaw=0 (no regression for any existing consumer); (3) correct world coords at ≥4 non-cardinal yaw values; (4) audit of duplicate screen↔world transform copies in `engine/render/` and input-side consumers complete; (5) `fleet-build --target IRShapeDebug` clean
  - **Issue:** #313
  - **Notes:** Child 5 of 5 of epic #310. Sequenced last — inverts the full composition once both T-055 (cardinal raster) and the residual composite pass (unfiled) land. The residual composite is a new child of #310 not yet filed as a GitHub issue; this task is blocked on it in addition to T-054. Full plan: `.fleet/plans/T-054.md`.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-046** — Audit: component-with-helper patterns across engine prefabs, codify rules · Owner: claude/T-046-component-helper-audit · PR: https://github.com/jakildev/IrredenEngine/pull/319
- [x] **T-045** — Fleet: stacked-PR: TASKS.md Stack: field for chain visibility · Owner: claude/T-045-stack-field-task-template · PR: https://github.com/jakildev/IrredenEngine/pull/318
- [x] **T-049** — Modifier framework: design doc + audit + framework declarations · Owner: claude/T-049-modifier-framework-foundation · PR: https://github.com/jakildev/IrredenEngine/pull/315
- [x] **T-047** — Engine CLAUDE.md style: add "prefer enums over strings" rule · Owner: claude/T-047-enum-style-rule · PR: https://github.com/jakildev/IrredenEngine/pull/314
- [x] **T-044** — Fleet: stacked-PR: downstream auto-rebase when upstream changes · Owner: claude/T-044-rebase-downstream · PR: https://github.com/jakildev/IrredenEngine/pull/308
- [x] **T-043** — Fleet: stacked-PR: reviewer upstream approval gating · Owner: claude/T-043-reviewer-upstream-gating · PR: https://github.com/jakildev/IrredenEngine/pull/301
- [x] **T-040** — Fleet: trigger-aware back-off in fleet-babysit · Owner: claude/T-040-trigger-aware-backoff · PR: https://github.com/jakildev/IrredenEngine/pull/300
- [x] **T-039** — Fleet: roles read scout cache instead of running gh/git directly · Owner: claude/T-039-roles-read-scout-cache · PR: https://github.com/jakildev/IrredenEngine/pull/296
- [x] **T-042** — Fleet: stacked-PR: start-next-task stack-aware reset · Owner: claude/T-042-start-next-task-stack-aware · PR: https://github.com/jakildev/IrredenEngine/pull/295
- [x] **T-001** — Linux build maturation: Linux CI build job added · Owner: claude/T-001-linux-ci · PR: https://github.com/jakildev/IrredenEngine/pull/297
- [x] **T-041** — Fleet: stacked-PR: commit-and-push stack-aware mode · Owner: claude/T-041-stacked-pr-skill · PR: https://github.com/jakildev/IrredenEngine/pull/292
- [x] **T-038** — Fleet: add fleet-state-scout daemon for shared state caching · Owner: claude/T-038-fleet-state-scout · PR: https://github.com/jakildev/IrredenEngine/pull/291
- [x] **T-037** — Fleet/merger: stacked-PR awareness via baseRefName · Owner: T-037-merger-stacked-awareness · PR: https://github.com/jakildev/IrredenEngine/pull/290
- [x] **T-035** — Prefab refactor: relocate debug overlay API from IRRender:: to prefab namespace · Owner: T-035-debug-overlay-prefab · PR: https://github.com/jakildev/IrredenEngine/pull/276
- [x] **T-034** — Prefab refactor: relocate fog-of-war API from IRRender:: to prefab namespace · Owner: T-034-fog-prefab-namespace · PR: https://github.com/jakildev/IrredenEngine/pull/275
- [x] **T-036** — Prefab refactor: relocate sun lighting API from IRRender:: to prefab namespace · Owner: T-036-sun-prefab-namespace · PR: https://github.com/jakildev/IrredenEngine/pull/278
- [x] **T-032** — Remove engine-side midi_polyrhythm demo after game port lands · Owner: T-032-remove-midi-polyrhythm · PR: https://github.com/jakildev/IrredenEngine/pull/274
- [x] **T-033** — engine/render CLAUDE.md: install layering principle between render and prefabs · Owner: T-033-render-prefab-layering-doc · PR: https://github.com/jakildev/IrredenEngine/pull/267
- [x] **T-029** — Fleet: cross-host smoke-test running-tally for render changes · Owner: T-029-cross-host-smoke-tally · PR: https://github.com/jakildev/IrredenEngine/pull/262
- [x] **T-007** — Wire up a `backend-parity` dry run · Owner: metal-finish-parity · PR: https://github.com/jakildev/IrredenEngine/pull/260
