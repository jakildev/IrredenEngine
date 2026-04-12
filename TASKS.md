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

Don't try to engineer this away with file locking or external state. The
PR-list cross-check plus GitHub's merge-conflict detection handles it
cheaply. If collisions become frequent enough to be painful, the upgrade
path is GitHub Issues as the queue with labels for claims — but only do
that when the pain is real.

This file is the **engine-level** task queue. Private creations that live
under `creations/` may define their own `TASKS.md` inside their own
directory — those are tracked independently and should not be mixed here.
Do not queue game or creation-specific gameplay tasks in this file; queue
them in the creation's own `TASKS.md`.

## Task template

```
- [ ] **<short title>** — <one-line goal>
  - **Area:** engine/render | engine/entity | engine/prefabs/... | docs | build | creations/demos/... | ...
  - **Model:** opus | sonnet  (which model should run this)
  - **Owner:** free | <worktree-name>
  - **Blocked by:** (none) | <title of blocking task>
  - **Acceptance:** <concrete check: build passes, test X passes, PR merged, screenshot Y looks like Z>
  - **Notes:** <context, links, prior attempts>
  - **Links:** (fill in PR URL when done)
```

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
  moving X out of the hot header"
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

- [ ] **Linux build maturation: get `linux-debug` preset green end-to-end** —
  fix every compile/link/runtime issue encountered when building the
  engine against the new `linux-debug` CMake preset inside WSL2 Ubuntu
  24.04. This is the umbrella epic — break it into smaller follow-up
  tasks as concrete issues are found.
  - **Area:** engine/* (anywhere the Linux path breaks)
  - **Model:** opus (for anything touching core-engine invariants) /
    sonnet (for mechanical port fixes like missing `#include`, case-
    sensitive paths, EOL drift)
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** from a fresh WSL2 Ubuntu 24.04 clone,
    `cmake --preset linux-debug && cmake --build build -j$(nproc)`
    builds `IRShapeDebug`, `IRCreationDefault`, and `IrredenEngineTest`
    with zero warnings escalated to errors, and `IRShapeDebug` launches
    via WSLg without crashing on its first frame.
  - **Notes:** the engine was originally written against Windows +
    MSYS2. Expect missing Linux branches under `#ifdef _WIN32`, missing
    system libs (update the apt list in `docs/AGENT_FLEET_SETUP.md` §1a
    as you find them), case-sensitive include mismatches, and EOL
    drift. Every real fix should land as its own dedicated PR — do
    **not** bundle unrelated Linux-port fixes into feature work.
    Reference `docs/AGENT_FLEET_SETUP.md` §10 for the list of known
    first-time issues.
  - **Links:**

- [ ] **macOS/Metal build maturation: get `macos-debug` preset green end-to-end** —
  mirror of the Linux-maturation task, on the Mac side. Umbrella epic
  for fixing every compile/link/runtime issue in the Metal backend
  as the fleet surfaces them. Must be run from a macOS host (Apple
  Silicon or Intel) — the Metal backend can't be cross-compiled.
  - **Area:** engine/render/src/metal, engine/render/src/shaders/metal,
    anywhere the Metal path breaks
  - **Model:** opus (backend/render work is Opus territory)
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** from a fresh macOS clone at `~/src/IrredenEngine`,
    `cmake --preset macos-debug && cmake --build build -j$(sysctl -n hw.ncpu)`
    builds `IRShapeDebug`, `IRCreationDefault`, and `IrredenEngineTest`
    with zero warnings escalated to errors, and `IRShapeDebug` launches
    and renders the same reference frame as the OpenGL backend.
  - **Notes:** see `docs/AGENT_FLEET_SETUP.md` §10 (macOS subsection)
    for the known first-time issues on macOS — Objective-C++ flags,
    shader parity gaps, FFmpeg `pkg-config` path, Metal 3 target
    version, Retina scaling. Unlike Linux maturation, shader parity
    gaps should be handed to the `backend-parity` skill as dedicated
    parity PRs (see the three concrete MSL port tasks below).
  - **Links:**

- [ ] **Metal parity: port `c_shapes_to_trixel.glsl` to MSL** —
  the GLSL compute for writing 2D shape SDFs into trixel canvases has
  no Metal counterpart. Invoke the `backend-parity` skill on a macOS
  host and port it.
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** macOS/Metal build maturation (need the preset to
    build cleanly before you can prove the port works end-to-end)
  - **Acceptance:** `engine/render/src/shaders/metal/c_shapes_to_trixel.metal`
    exists and matches the GLSL binding/uniform layout, the
    `macos-debug` build is clean, and the shapes-rendering demo
    (whichever creation exercises `SHAPES_TO_TRIXEL`) renders
    visually identically to the OpenGL version.
  - **Notes:** read `engine/render/CLAUDE.md` pipeline overview and
    the GLSL source in full before porting. Use the cheatsheet in
    `.claude/skills/backend-parity/SKILL.md`.
  - **Links:**

- [ ] **Metal parity: port `c_update_voxel_positions.glsl` to MSL** —
  GPU-side voxel-position update compute with no Metal counterpart.
  Same skill flow as above.
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** macOS/Metal build maturation
  - **Acceptance:** `engine/render/src/shaders/metal/c_update_voxel_positions.metal`
    exists and matches the GLSL binding/uniform layout, `macos-debug`
    build is clean, and a voxel-animation demo (e.g. one of the
    `creations/demos/*` that exercises moving voxels) animates
    identically to the OpenGL version.
  - **Notes:** dispatch size and buffer bindings must match exactly
    — any mismatch will race or corrupt the voxel pool. This one is
    higher-stakes than `c_shapes_to_trixel`; be thorough.
  - **Links:**

- [ ] **Metal parity: port `c_voxel_visibility_compact.glsl` to MSL** —
  the voxel-visibility compaction pass (reduces visible-voxel indices
  into a dense buffer) has no Metal counterpart. Same skill flow.
  - **Area:** engine/render/src/shaders/metal
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** macOS/Metal build maturation
  - **Acceptance:** `engine/render/src/shaders/metal/c_voxel_visibility_compact.metal`
    exists, matches the GLSL binding/uniform layout, `macos-debug`
    build is clean, and `IRShapeDebug` on Metal gives the same
    visible-voxel counts per frame as the OpenGL version.
  - **Notes:** compaction shaders rely on atomic operations — pay
    attention to the `atomicAdd` → `atomic_fetch_add_explicit`
    translation and the memory-order argument (use `relaxed` unless
    the GLSL source implies otherwise). See the cheatsheet in the
    `backend-parity` skill.
  - **Links:**

- [ ] **Wire up a `backend-parity` dry run** — use the new
  `backend-parity` skill end-to-end on a known-small parity gap
  (pick one of the three MSL port tasks above, ideally
  `c_shapes_to_trixel` as the least invasive). This is the equivalent
  of the "example" tasks below — the goal is to exercise the skill's
  flow and catch any workflow bugs before running it on real
  production gaps.
  - **Area:** tooling
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** macOS/Metal build maturation
  - **Acceptance:** one parity PR opened, reviewed by `review-pr`,
    merged. Any workflow bugs in the `backend-parity` skill itself
    are filed as follow-up `[sonnet]` tasks to fix the skill.
  - **Notes:** treat any skill failure as a skill bug, not a task
    failure. The point of this dry run is to uncover workflow
    issues, not to ship the parity port (that happens as a natural
    side effect).
  - **Links:**

- [ ] **Example: benchmark IRShapeDebug at zoom 4** — measure per-system
  timing and file a report.
  - **Area:** engine/profile
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** Linux build maturation (if running in the fleet;
    the Windows-native clone can start this immediately)
  - **Acceptance:** `docs/perf-reports/shape_debug_zoom4.md` exists with
    per-system `easy_profiler` screenshots and a 1-paragraph summary of the
    top 3 hotspots.
  - **Notes:** use `IRShapeDebug` from `creations/demos/shape_debug/`. See
    `engine/profile/CLAUDE.md` for the macros and how to enable the
    profiler build flag.
  - **Links:**

- [ ] **Example: unit tests for engine/math/physics.hpp** — exhaustive
  tests for ballistic helpers.
  - **Area:** engine/math
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none — unit tests don't care which platform builds them)
  - **Acceptance:** new test binary builds, tests cover all four physics
    helpers (`impulseForHeight`, `flightTimeForHeight`, `heightForImpulse`,
    `isTunnelingSafe`) with edge cases, all pass.
  - **Notes:** pattern-heavy; the function spec is in
    `engine/math/CLAUDE.md` under "Physics". If a test uncovers a real bug
    in the helpers, stop and requeue as `[opus]` with a bug report rather
    than fixing inline.
  - **Links:**

- [ ] **Unit tests: expand EntityManager coverage** — add tests for
  the EntityManager operations not yet covered by
  `entity_manager_test.cpp`.
  - **Area:** test/entity
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** `test/entity/entity_manager_test.cpp` covers: entity
    create/destroy lifecycle, entity pool recycling, named entities
    (`setName`/`getEntityByName`), `destroyAllEntities`, entity flags
    (`setFlags`, `markEntityForDeletion`, `destroyMarkedEntities`),
    and archetype migration when components are added/removed via
    `setComponent`/`removeComponentById`. All tests pass.
  - **Notes:** follow the conventions in `test/CLAUDE.md`. First, move
    the existing file from `test/ecs/entity_manager_test.cpp` to
    `test/entity/entity_manager_test.cpp` and update the source path
    in `test/CMakeLists.txt`. Then extend the existing `IREntityTest`
    fixture. The constructor sets `g_entityManager`, so both
    member-instance and free-function calls work. Existing tests cover
    deferred removal and simple removal — focus on the gaps. If a
    test uncovers a real bug, stop and requeue as `[opus]`.
  - **Links:**

- [ ] **Unit tests: archetype and archetype_node** — test archetype
  creation, component string formatting, and ArchetypeNode construction
  and child-of relation lookup.
  - **Area:** test/entity
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** new file `test/entity/archetype_test.cpp` added to
    `test/CMakeLists.txt`, covers: `makeComponentStringInternal`
    formatting, `ArchetypeNode` construction with pure components,
    `getChildOfRelation` returning `kNullRelation` when no child-of
    relation is present. All tests pass.
  - **Notes:** `archetype.cpp` is small (just string formatting) and
    `archetype_node.cpp` is small (constructor + one query). Combine
    into one test file. Use an `EntityManager` fixture since
    `ArchetypeNode` construction calls `isPureComponent` and
    `createComponentData` which need the global entity manager. Follow
    `test/CLAUDE.md` conventions.
  - **Links:**

- [ ] **Unit tests: ArchetypeGraph** — test graph construction, node
  lookup/creation, edge wiring, and the BFS relation sort.
  - **Area:** test/entity
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** new file `test/entity/archetype_graph_test.cpp`
    added to `test/CMakeLists.txt`, covers: base node exists after
    construction, `findArchetypeNode` returns nullptr for unknown type,
    `findCreateArchetypeNode` creates and wires add/remove edges,
    repeated `findCreateArchetypeNode` returns the same node,
    `sortArchetypeNodesByRelationChildOf` orders parents before
    children. All tests pass.
  - **Notes:** this is `[opus]` because the graph traversal logic,
    edge wiring, and BFS sort are subtle — a Sonnet agent could
    miss edge cases around shared intermediate nodes or the
    unused-but-present `createArchetypeNodeWithArchetype` /
    `connectNodeToBase` / `createNodeChainToBase` functions. Need an
    `EntityManager` fixture for the global state. Follow
    `test/CLAUDE.md` conventions.
  - **Links:**

- [ ] **Unit tests: ir_entity free-function API** — test the global
  facade functions in `engine/entity/src/ir_entity.cpp`.
  - **Area:** test/entity
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** new file `test/entity/ir_entity_test.cpp` added to
    `test/CMakeLists.txt`, covers: `createEntity`, `destroyEntity`,
    `destroyAllEntities`, `makeComponentString`, `setParent` /
    `getParentEntityFromArchetype` relation wiring, `setName` /
    `getEntity` named-entity round-trip. All tests pass.
  - **Notes:** these are thin wrappers over `EntityManager` — the
    tests verify the global wiring works, not the underlying logic
    (that's covered by the EntityManager tests). Keep tests focused
    on "does the facade route correctly." The old stub
    `ir_entity_test.cpp` was deleted in the test-conventions PR —
    create a fresh file. Follow `test/CLAUDE.md` conventions.
  - **Links:**

- [ ] **Unit tests: SystemManager** — test system registration,
  pipeline execution order, and relation tick dispatch.
  - **Area:** test/system
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** new file `test/system/system_manager_test.cpp`
    added to `test/CMakeLists.txt`, covers: `registerPipeline` stores
    the pipeline, `executePipeline` calls systems in order and flushes
    structural changes, `handleRelationTick` fires once per unique
    related entity. All tests pass.
  - **Notes:** this is `[opus]` because the tick dispatch, relation
    tick deduplication, and interaction with `flushStructuralChanges`
    are subtle. The fixture needs both `EntityManager` and
    `SystemManager` initialized (both set their global pointers in
    their constructors). Creating test systems requires the
    `createSystem<>` template machinery — read
    `engine/system/CLAUDE.md` before writing tests. Follow
    `test/CLAUDE.md` conventions.
  - **Links:**

- [ ] **Unit tests: math headers (easing, color, bezier)** — test
  pure-function math helpers in the header-only math utilities.
  - **Area:** test/math
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** (none)
  - **Acceptance:** new file(s) under `test/math/` added to
    `test/CMakeLists.txt`, covering: easing functions
    (`engine/math/include/irreden/math/easing_functions.hpp`) —
    boundary values (t=0 returns 0, t=1 returns 1), monotonicity for
    a sampling of t values; color helpers
    (`engine/math/include/irreden/math/color.hpp`) — round-trip
    conversions if any, boundary values; bezier curves
    (`engine/math/include/irreden/math/bezier_curves.hpp`) — endpoint
    interpolation (t=0 returns start, t=1 returns end). All tests pass.
  - **Notes:** these are pure functions with no global state — use
    plain `TEST()`, no fixture needed. `kTolerance` for float
    comparisons. The `ir_math.cpp` iso-math tests are being handled
    on a separate PR (jakildev/IrredenEngine#79) — do not duplicate
    that work. Focus only on the header-only helpers listed above.
    Follow `test/CLAUDE.md` conventions.
  - **Links:**

---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->

(none yet)

---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

(none yet)
