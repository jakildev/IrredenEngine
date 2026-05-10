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
requeue with `[opus]`. [`docs/agents/FLEET.md`](docs/agents/FLEET.md) "Model split" has the full split.

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

- [~] **Lua-driven ECS: Lua port of perf_grid + perf parity gate** — new demo creations/demos/lua_perf_grid/ mirroring perf_grid (262k entities, wave animation, same render pipeline) entirely in Lua; parity gate: Lua wave-animation per-tick cost <= 1.5x C++ equivalent
  - **ID:** T-104
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** claude/T-104-lua-perf-grid
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-build --target IRLuaPerfGrid clean on linux-debug; (2) fleet-run IRLuaPerfGrid runs without crash (64x64x64 voxel grid, wave animation, same render pipeline as perf_grid); (3) parity gate: Lua wave-animation system per-tick cost <= 1.5x C++ SystemPeriodicIdlePositionOffset per-tick cost measured via IRProfile with profiling_enabled=true; (4) measured ratio documented in docs/design/lua-driven-ecs.md retrospective; (5) if gate fails: design doc PR amended with corrective decision before further work
  - **Issue:** #492
  - **Notes:** PR 6 of 6 for parent epic #293 — formal acceptance gate for the entire Lua-driven ECS stack. Full architect plan in .fleet/plans/T-104.md. Blocked by T-103 (hot-reload). If parity gate fails, this PR does not merge; instead amend T-099's design doc with corrective decision (LuaJIT migration, codegen-bound bodies, etc.).
  - **Links:**




- [~] **Render: HDR pipeline — RGBA16F canvas, tonemap pass, exposure control, sky term** — grow LDR pipeline into HDR; RGBA16F canvas color attachment; tonemap pass between LIGHTING_TO_TRIXEL and TRIXEL_TO_FRAMEBUFFER; exposure uniform; additive sky-term from emissive top hemisphere
  - **ID:** T-118
  - **Area:** engine/render, shaders/glsl, shaders/metal
  - **Model:** opus
  - **Owner:** claude/T-118-hdr-pipeline
  - **Blocked by:** (none)
  - **Acceptance:** (1) bright emissive lights no longer clip at white; saturation preserved through lighting → tonemap chain; (2) new lighting demo (IRLightingHDR or similar) exercises full HDR pipeline; (3) existing lighting demos (IRLightingCombined, IRLightingPoint, IRLightingSpot, IRLightingEmissive, IRLightingSunShadow) look identical to pre-HDR LDR output at default exposure; (4) fleet-build clean on linux-debug AND macos-debug
  - **Issue:** #366
  - **Notes:** Follow-up from lighting-fidelity-polish PR (audit findings #35-#38). Not in the lighting-fidelity-polish PR because HDR is a separate correctness dimension requiring its own tonemap tuning, demo screenshots, and perf measurement. Pick one tonemap operator and ship it (Reinhard, ACES, or Uncharted-2). Sky term: emissive top hemisphere driving additive contribution that cuts off at occlusion — cheap and visually impactful.
  - **Links:**


- [~] **Codegen pipeline foundation — components only** — build the Lua → C++ codegen tool that emits component definitions from Lua schemas at build time
  - **ID:** T-106
  - **Area:** engine/script, build
  - **Model:** opus
  - **Owner:** claude/T-106-lua-codegen-foundation
  - **Blocked by:** (none)
  - **Stack:** T-106..T-109 codegen-pipeline
  - **Acceptance:** (1) new codegen tool at cmake/lua_codegen/ — takes list of .lua files + output .hpp path, language choice documented in PR body; (2) emits struct C_Name in namespace IRComponents + matching _lua.hpp binding (kHasLuaBinding<C_Name>, bindLuaType<C_Name>, sol::usertype with field accessors) + registration helper calling IREntity::EntityManager::registerComponent<C_Name>() for every IRComponent.register("Name", {...}) call found; (3) type inference: int→int32_t, float, bool, string; explicit typed form ({field = {type="float", default=100}}) honored; unsupported defaults error with file/line/field name; (4) CMake helper irreden_lua_codegen(<target> SOURCES <lua_files...> OUTPUT_HPP <path>) with add_custom_command in cmake/ir_functions.cmake; (5) smoke test: C_Hp{current=100, max=100} round-trip (Lua creates entity → C++ reads back) passes; (6) test/script/lua_component_register_test.cpp passes under CODEGEN mode (test-harness toggle EVAL vs. CODEGEN); (7) fleet-build --target IrredenEngineTest clean + all existing tests pass
  - **Issue:** #586
  - **Notes:** PR 1 of 4 for Lua codegen stack (T-106..T-109 codegen-pipeline). Blocked by T-105 (LuaJIT migration). Architect decision in #566; design plan at ~/.claude/plans/fuzzy-singing-stardust.md. Precedent for add_custom_command shape: creations/demos/lua_pipeline_demo/CMakeLists.txt lines 21–36. Out of scope: system bodies (T-107), mode override (T-108), demo migration (T-109).
  - **Links:**


- [~] **Codegen system bodies — DSL parser + C++ emitter** — extend codegen tool to handle Lua system bodies; define DSL subset with strict build-time error for anything outside it
  - **ID:** T-107
  - **Area:** engine/script, build
  - **Model:** opus
  - **Owner:** claude/T-107-codegen-system-bodies
  - **Blocked by:** T-106
  - **Stack:** T-106..T-109 codegen-pipeline
  - **Acceptance:** (1) codegen tool parses DSL subset: canonical loop (for i=0, arch.length-1 do), column ops (arch.Component:at/setAt/getField/setField), Component.new(), arithmetic/comparison/logical ops, if/elseif/else, local declarations, whitelisted intrinsics; (2) emits template<> struct System<NAME> specialisations in namespace IRSystem with tick(arch_ref) member + static SystemId create() using registerSystem<> (if T-136 landed) or createSystem+setSystemParams; (3) out-of-DSL features produce build errors with file/line/feature name; test cases assert specific error messages for: closures, metatables, require, unsupported loop forms; (4) intrinsic whitelist config table (Lua→C++ template): math.sin/cos/tan/atan/sqrt/abs/floor/ceil/min/max + IRMath.lerp/clamp; extensible by adding table entries; (5) test/script/lua_system_register_test.cpp passes under CODEGEN mode; (6) fleet-build --target IrredenEngineTest clean + all existing tests pass
  - **Issue:** #587
  - **Notes:** PR 2 of 4 for Lua codegen stack (T-106..T-109 codegen-pipeline). Blocked by T-106. Architect decision in #566; design plan at ~/.claude/plans/fuzzy-singing-stardust.md. Out of scope: per-system mode override (T-108), demo migration (T-109).
  - **Links:**


- [~] **Per-system mode override + CODEGEN/EVAL coexistence in one creation** — add mode field so CODEGEN and EVAL systems can coexist; T-103 hot-reload stays EVAL-only
  - **ID:** T-108
  - **Area:** engine/script, build
  - **Model:** opus
  - **Owner:** claude/T-108-mode-override-coexistence
  - **Blocked by:** T-107
  - **Stack:** T-106..T-109 codegen-pipeline
  - **Acceptance:** (1) IRSystem.registerSystem({mode="eval"|"codegen",...}) opts system out/in; absent mode uses creation default; unknown values are codegen errors; (2) CMake IR_LUA_ECS_DEFAULT_MODE cache var per creation (CODEGEN default or EVAL); irreden_lua_codegen helper gains DEFAULT_MODE param; (3) EVAL-mode systems emit stubs calling bindLuaDrivenEcs() → IRSystem::createSystemDynamic() at runtime; CODEGEN systems emit C++ as in T-107; same SystemId space; (4) coexistence test: two systems in one Lua file (one CODEGEN, one EVAL) — both register, both tick, EVAL is hot-reloadable, CODEGEN hot-reload attempt errors with "hot-reload not supported in CODEGEN mode for system 'Name'; mark mode='eval' or rebuild"; (5) T-103 hot-reload continues working for EVAL systems; (6) engine/script/CLAUDE.md updated with mode semantics + CMake flag + hot-reload-only-in-EVAL contract; engine/CLAUDE.md style section gets CODEGEN-as-default bullet; (7) fleet-build clean under both IR_LUA_ECS_DEFAULT_MODE=CODEGEN and =EVAL; all tests pass under both
  - **Issue:** #588
  - **Notes:** PR 3 of 4 for Lua codegen stack (T-106..T-109 codegen-pipeline). Blocked by T-107. Architect decision in #566; design plan at ~/.claude/plans/fuzzy-singing-stardust.md. Out of scope: demo migration (T-109).
  - **Links:**


- [~] **Migrate Lua perf-grid to CODEGEN, re-run parity gate, close #293** — apply CODEGEN to creations/demos/lua_perf_grid/, re-profile vs. C++ perf_grid, close Lua-driven ECS epic
  - **ID:** T-109
  - **Area:** engine/script, creations/demos/lua_perf_grid
  - **Model:** opus
  - **Owner:** claude/T-109-codegen-perf-grid
  - **Blocked by:** T-108
  - **Stack:** T-106..T-109 codegen-pipeline
  - **Acceptance:** (1) lua_perf_grid/ migrated to CODEGEN; fleet-build --target IRLuaPerfGrid clean under IR_LUA_ECS_DEFAULT_MODE=CODEGEN; fleet-run produces same populated lattice as C++ baseline; (2) CODEGEN wave-system per-tick measured at 16³ (4096 entities) and 64³ (262144 entities): ≤1.5× C++ perf_grid wave system; actual ratio documented; (3) EVAL build (IR_LUA_ECS_DEFAULT_MODE=EVAL) profiled same configs: ~2–10× C++ (informational, not a gate); (4) docs/design/lua-driven-ecs.md retrospective updated: original ≥10000× failure, LuaJIT-only intermediate, CODEGEN final (gate number), architect rationale; (5) PR #563 rebased + body amended with measurements, merged; epic #293 amended + closed; #566 closed; (6) fleet-build --target IrredenEngineTest clean under both modes; full test suite passes. If gate fails (>1.5×): do NOT close #293; amend docs/design/lua-driven-ecs.md with corrective decision instead
  - **Issue:** #589
  - **Notes:** PR 4 of 4 (final) for Lua codegen stack (T-106..T-109 codegen-pipeline); closes Lua-driven ECS epic #293 and corrective-decision issue #566. Blocked by T-108. Design plan at ~/.claude/plans/fuzzy-singing-stardust.md. Predecessors: #585 (T-105), #586 (T-106), #587 (T-107), #588 (T-108).
  - **Links:**


---

## In progress

<!-- Tasks currently being worked on. Mirror of [~] items above. -->


---

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-105** — LuaJIT 2.1 runtime migration · Owner: claude/T-105-luajit-runtime · PR: https://github.com/jakildev/IrredenEngine/pull/595
- [x] **T-135** — fleet-up.conf concurrency cap · Owner: claude/T-135-fleet-up-conf-concurrency-cap · PR: https://github.com/jakildev/IrredenEngine/pull/594
- [x] **T-138** — fleet-claim: atomic master-side TASKS.md lock · Owner: claude/T-138-fleet-claim-master-lock · PR: https://github.com/jakildev/IrredenEngine/pull/593
- [x] **T-136** — Systems: registerSystem<N> helper to retire Params + setSystemParams boilerplate · Owner: claude/T-136-register-system-helper · PR: https://github.com/jakildev/IrredenEngine/pull/592
- [x] **T-137** — fleet-claim: hard model-tag gate · Owner: claude/T-137-fleet-claim-model-gate · PR: https://github.com/jakildev/IrredenEngine/pull/591
- [x] **T-134** — C_SpriteSheet: onDestroy GPU texture cleanup · Owner: claude/T-134-sprite-sheet-on-destroy · PR: https://github.com/jakildev/IrredenEngine/pull/590
- [x] **T-133** — Render: 2x2 PCF in screen-space sun shadow lookup · Owner: claude/T-133-2x2-pcf-sun-shadow · PR: https://github.com/jakildev/IrredenEngine/pull/574
- [x] **T-130** — Hover: unify GUI / world / trixel sources in SYSTEM_ENTITY_HOVER_DETECT · Owner: claude/T-130-gui-hitbox-source · PR: https://github.com/jakildev/IrredenEngine/pull/575
- [x] **T-131** — Render — widen iso rasterization to shadow-feeder AABB · Owner: claude/T-131-shadow-feeder-aabb · PR: https://github.com/jakildev/IrredenEngine/pull/576
- [x] **T-132** — Render: sun shadow normal-bias offset + slope-scale bias tuning · Owner: claude/T-132-shadow-normal-bias · PR: https://github.com/jakildev/IrredenEngine/pull/573
- [x] **T-103** — Lua-driven ECS — hot-reload of Lua system bodies · Owner: claude/T-103-lua-system-body-hot-reload · PR: https://github.com/jakildev/IrredenEngine/pull/559
- [x] **T-092** — Render: final occupancy-grid teardown — comment cleanup · Owner: claude/T-092-occupancy-grid-teardown · PR: https://github.com/jakildev/IrredenEngine/pull/567
- [x] **T-129** — Fleet: fleet-up bootstrap-trigger extension for worker/reviewer roles · Owner: claude/T-129-fleet-up-bootstrap-triggers · PR: https://github.com/jakildev/IrredenEngine/pull/562
- [x] **T-128** — Fleet: demote queue-manager to scout-driven script — add fleet-queue-tick, remove LLM pane · Owner: claude/T-128-relanded · PR: https://github.com/jakildev/IrredenEngine/pull/564
- [x] **T-115** — Docs: cross-author stacking lifecycle in FLEET.md · Owner: claude/T-115-cross-author-stacking-docs · PR: https://github.com/jakildev/IrredenEngine/pull/557
- [x] **T-127** — Fleet: queue-manager role doc — replace hand-edit loop with fleet-tasks-render call · Owner: claude/T-127-queue-manager-fleet-tasks-render · PR: https://github.com/jakildev/IrredenEngine/pull/556
- [x] **T-126** — Render: migrate light-volume propagation off CPU-built OccupancyGrid SSBO · Owner: claude/T-126-occupancy-ssbo-decouple · PR: https://github.com/jakildev/IrredenEngine/pull/546
- [x] **T-125** — Fleet: per-role concurrency cap config + dispatcher enforcement · Owner: claude/T-125-fleet-concurrency-cap · PR: https://github.com/jakildev/IrredenEngine/pull/548
- [x] **T-112** — Worker role docs: stackable-blocked fallback pickup tier · Owner: claude/T-112-stackable-blocked-pickup · PR: https://github.com/jakildev/IrredenEngine/pull/545
- [x] **T-102** — Lua-driven ECS: pipeline composition + enum bindings + modifier-framework bindings · Owner: claude/T-102-lua-pipeline-modifier-bindings · PR: https://github.com/jakildev/IrredenEngine/pull/544
