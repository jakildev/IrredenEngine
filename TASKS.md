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



- [~] **Editor F-0.6: per-voxel metadata extension (.txl v2)** — add material_id, flags, bone_id per voxel; bump .txl to v2 with version-aware loader preserving backward compat
  - **ID:** T-146
  - **Area:** engine/prefabs/irreden/voxel, creations/editors
  - **Model:** opus
  - **Owner:** claude/T-146-txl-v2-metadata
  - **Blocked by:** (none)
  - **Acceptance:** (1) .txl v2 format spec written and committed alongside loader; (2) version-aware loader: reads v1 unchanged (zero-fills new fields), reads v2 with full fidelity, rejects unknown versions with clear error; (3) round-trip test: load v1 → save as v2 → reload v2 → equality on original voxel data; (4) existing demos/assets loading .txl keep working without code changes; (5) accessor API: voxel.material_id(), voxel.flags(), voxel.bone_id() on engine voxel type; (6) fleet-build clean on linux-debug
  - **Issue:** #621
  - **Notes:** Phase 0 (F-0.6) of entity-editor epic. Parent epic: #603. Umbrella: #213. Epic doc: #619. Engine reserves full byte for material_id (16–32 cap is game-side concern per irreden#60). Per-voxel size growth has perf implications worth measuring at implementation time.
  - **Links:**


- [~] **Editor F-0.8: editor exe scaffold at creations/editors/voxel_editor/** — stand up IRVoxelEditor target with main loop, window, empty dockspace, and 3D viewport reference grid
  - **ID:** T-147
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-147-voxel-editor-scaffold
  - **Blocked by:** (none)
  - **Acceptance:** (1) fleet-build --target IRVoxelEditor clean on linux-debug; (2) fleet-run IRVoxelEditor launches window with 3D viewport showing reference grid; (3) window close + Esc both shut cleanly; (4) source layout follows creations/demos/shape_debug/ patterns per creations/CLAUDE.md; (5) builds on macos-debug and windows-debug
  - **Issue:** #622
  - **Notes:** Phase 0 (F-0.8) of entity-editor epic. Parent epic: #603. Umbrella: #213. All later Phase 0 tickets (F-0.4 camera, F-0.5 gizmos, F-0.9 picking) and Phase 1+ live inside this exe. Reference: creations/demos/shape_debug/CMakeLists.txt.
  - **Links:**


- [~] **Editor F-0.2: layout system (rows, columns, dock targets, splitters)** — flex-like containers, draggable splitters, dockable panels building the editor dockspace on top of F-0.1 primitives
  - **ID:** T-148
  - **Area:** engine/prefabs/irreden/render, creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-148-layout-system
  - **Blocked by:** (none)
  - **Acceptance:** (1) compose fixed editor dockspace (left + center viewport + right + bottom) using layout API; (2) splitters resize live with min/max constraints respected; (3) drag panel by title bar → dock-target previews appear → drop to dock; layout persists in memory until shutdown; (4) layout serializes/deserializes from JSON blob in-memory (file IO deferred, serialization path must work)
  - **Issue:** #623
  - **Notes:** Phase 0 (F-0.2) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-145 (F-0.1 UI primitives). Every editor panel composes through this.
  - **Links:**


- [~] **Editor F-0.3: input routing (mouse hover/click/drag, keyboard focus, hotkey table)** — z-order-aware event dispatch to widgets, single-widget keyboard focus, capture/release, centralized hotkey registry
  - **ID:** T-149
  - **Area:** engine/prefabs/irreden/input, creations/editors
  - **Model:** sonnet
  - **Owner:** claude/T-149-input-routing
  - **Blocked by:** (none)
  - **Acceptance:** (1) click on button under panel overlay does NOT fire the button (z-order respected); (2) drag-from-slider updates value live; release outside slider rect still completes drag (capture works); (3) Tab cycles focus across text inputs in a panel; (4) register action editor.toggle_grid with Ctrl+G → fires on keypress; duplicate binding on same combo logs a warning
  - **Issue:** #624
  - **Notes:** Phase 0 (F-0.3) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-145 (F-0.1 UI primitives). Hotkey table unblocks Phase 1+ editor shortcuts (paint, erase, copy, paste, save).
  - **Links:**


- [ ] **Editor F-0.4: 3D editor camera (orbit + pan + zoom)** — editor viewport camera with orbit/pan/zoom/snap-views/persistence; human note: may pivot to entity-rotation vs camera-orbit approach
  - **ID:** T-150
  - **Area:** creations/editors
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-147
  - **Acceptance:** (1) editor opens with sane default framing showing reference grid centered; (2) orbit/pan/zoom/snap/re-center all work responsively at 60 fps on linux-debug; (3) close editor + re-open → framing restored; (4) snap views give pixel-aligned axis-aligned projection
  - **Issue:** #625
  - **Notes:** Phase 0 (F-0.4) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-147 (F-0.8 scaffold). Human note: engine has one yaw rotation so far; suggests voxel entity rotation rather than full camera orbit since entities turn in the world — verify approach with architect/human before full implementation. Look at existing demo cameras before designing from scratch.
  - **Links:**


- [ ] **Editor F-0.7: JSON sidecar format for .txl** — .txl.json alongside every .txl save; stores bind-points, component-pack fields, material-registry references; silent when absent
  - **ID:** T-151
  - **Area:** engine/prefabs/irreden/voxel, creations/editors
  - **Model:** sonnet
  - **Owner:** free
  - **Blocked by:** T-146
  - **Acceptance:** (1) editor writes .txl.json next to .txl save (omit file if content empty); (2) loader reads sidecar if present; missing sidecar = empty bind-point list, empty component-pack, identity material map, no log; (3) round-trip test: write voxel grid + bind-points → reload → bind-points match exactly; (4) schema documented in same doc as .txl v2 spec from F-0.6
  - **Issue:** #626
  - **Notes:** Phase 0 (F-0.7) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-146 (F-0.6 per-voxel metadata). Bind-point name vocabulary in game-side editor-needs.md (irreden#60). Engine stores component-pack values as untyped JSON; game side reads via its own registry.
  - **Links:**


- [ ] **Editor F-0.5: gizmo primitives (translate/rotate/scale handles, joint/bind-point/IK markers)** — screen-space-sized depth-aware 3D gizmos rendered into the editor viewport on top of the voxel scene
  - **ID:** T-152
  - **Area:** engine/render, creations/editors
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-150
  - **Acceptance:** (1) select voxel → translate gizmo at center; drag X arrow → moves only in X; release applies; (2) rotate gizmo drag around Y ring rotates selection around Y; Shift held snaps to 15°; (3) scale gizmo drag-uniform-center scales uniformly; (4) all gizmos render at constant screen-space size regardless of camera distance, depth-aware (hidden faces dimmed); (5) hover highlights handle under cursor; (6) joint/bind-point/IK marker primitives render at constant screen-space size and are clickable
  - **Issue:** #627
  - **Notes:** Phase 0 (F-0.5) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc) and T-150 (F-0.4 camera — gizmos need a working camera viewport). Render-pipeline integration required: custom shader pass for screen-space sizing and depth-aware rendering. Phase 2 (skeletal) and Phase 5 (bind-points) author rigs via these gizmos.
  - **Links:**


- [ ] **Editor F-0.9: voxel mouse picking (ray cast → world-space voxel selection)** — cursor-to-ray, DDA voxel grid intersection, single-voxel selection state with visual highlight
  - **ID:** T-153
  - **Area:** engine/render, creations/editors
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-147, T-150
  - **Acceptance:** (1) left-click on voxel in 3D viewport → selected and visually highlighted; (2) left-click on empty space → selection clears; (3) selection survives camera orbit/pan/zoom (only highlight redraws); (4) picking respects camera projection (orthographic + perspective); (5) selected voxel world-space position queryable via editor selection state for Phase 1+ tools
  - **Issue:** #628
  - **Notes:** Phase 0 (F-0.9) of entity-editor epic. Parent epic: #603. Umbrella: #213. Blocked by T-144 (epic doc), T-147 (F-0.8 editor scaffold), T-150 (F-0.4 camera). Selection-state design must anticipate Phase 1+ multi-selection. Phase 0 acceptance criterion in #603 explicitly requires mouse picking.
  - **Links:**


- [~] **GPU particles Phase 2: batch CPU-side Metal spawns** — accumulate per-frame spawn staging buffer in C_GPUParticlePool; eliminate per-spawn Metal buffer orphan overhead before continuous-emitter Lua API lands
  - **ID:** T-159
  - **Area:** engine/render, engine/prefabs/irreden/render, creations/demos/gpu_particles
  - **Model:** opus
  - **Owner:** claude/T-159-gpu-particle-spawn-batching
  - **Blocked by:** (none)
  - **Acceptance:** (1) continuous-emitter demo spawning N particles/frame at N ∈ {10, 100, 1000} reports steady-state Metal buffer subData allocation counts ≤ 1/frame regardless of N; (2) cross-backend visual parity (OpenGL vs Metal) under continuous emission; (3) fleet-build --target IRGpuParticles clean on both backends; (4) per-frame frame time on Metal not regressed vs OpenGL for same emission rate
  - **Issue:** #632
  - **Notes:** Phase 2 follow-up to PR #614 (T-139). Metal subData orphans whole MTL buffer per call; at 100 spawns/frame on 4096-slot pool that is ~12.8 MB/frame overhead. Approach (a): frame-scoped staging vector in C_GPUParticlePool — simplest, backend-agnostic. Approach (b): Metal BufferImpl frame-coherent batch mode — more invasive. OpenGL unaffected (glNamedBufferSubData is a true partial patch). CPU lifetime_ mirror silent-drop-after-4096-spawns is a separate carry-over.
  - **Links:**


- [~] **TEXT_TO_TRIXEL: hoist gui canvas lookup out of per-entity tick** — eliminate per-entity foreign-entity getComponent in TEXT_TO_TRIXEL::tick by caching canvas entity and textures in beginTick
  - **ID:** T-160
  - **Area:** engine/prefabs/irreden/render
  - **Model:** sonnet
  - **Owner:** sonnet-fleet-2
  - **Blocked by:** (none)
  - **Acceptance:** (1) IREntity::getComponent no longer appears in System<TEXT_TO_TRIXEL>::tick; (2) IRShapeDebug and any text-bearing demo render identically to pre-fix output (single-frame visual regression check); (3) fleet-build --target IRShapeDebug clean on linux-debug
  - **Issue:** #644
  - **Notes:** Pre-existing ECS hot-loop smell surfaced as nit N2 in T-156 Opus recheck (PR #638). Hoist EntityId guiCanvas_ and C_TriangleCanvasTextures* as members on System<TEXT_TO_TRIXEL>; resolve in beginTick; tick reads cached value. system_text_to_trixel.hpp:210-211 is the change site. Related duplicate: #642 (flagged fleet:needs-info).
  - **Links:**


- [~] **C_CanvasFogOfWar: evaluate per-write subData vs full-buffer dirty-flag upload** — profile fog upload cost under realistic workloads; implement faster strategy or document rationale for deferral
  - **ID:** T-161
  - **Area:** engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** claude/T-161-fog-upload-eval
  - **Blocked by:** (none)
  - **Acceptance:** (1) per-frame fog upload cost measured under three workloads: single-cell update, radius reveal, full clear; (2) code change implementing chosen strategy OR note in design doc deferring change with rationale; (3) IRFogOfWar (or equivalent demo) renders identically
  - **Issue:** #645
  - **Notes:** Pre-existing pattern surfaced as nit N3 in T-156 Opus recheck (PR #638). Current: dirty_ flag gates whole-buffer subImage2D upload in FOG_TO_TRIXEL::tick. Alternative: per-write subData with dirty tile list in C_CanvasFogOfWar::setCell. Tradeoff: per-write cheaper for few-cell updates, whole-buffer cheaper for large diffs. Measurement required before implementation. Related duplicate: #643 (flagged fleet:needs-info).
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-162** — engine/entity: ECS singleton-component infrastructure · Owner: claude/T-162-ecs-singleton · PR: https://github.com/jakildev/IrredenEngine/pull/650
- [x] **T-157** — Migrate lighting + debug cluster to member-on-System<N> · Owner: claude/T-157-lighting-debug-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/640
- [x] **T-158** — Migrate final composite + sprites cluster to member-on-System<N> · Owner: claude/T-158-final-composite-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/639
- [x] **T-156** — Migrate trixel-canvas content systems to member-on-System<N> · Owner: claude/T-156-trixel-canvas-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/638
- [x] **T-155** — Migrate GPU-compute cluster to member-on-System<N> · Owner: claude/T-155-gpu-compute-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/637
- [x] **T-154** — Migrate hitbox GUI system to member-on-System<N> · Owner: claude/T-154-hitbox-gui-register-system · PR: https://github.com/jakildev/IrredenEngine/pull/636
- [x] **T-145** — Editor F-0.1: trixel UI primitives · Owner: claude/T-145-trixel-ui-primitives · PR: https://github.com/jakildev/IrredenEngine/pull/631
- [x] **T-144** — Docs: land entity-editor-epic.md canonical reference · Owner: claude/T-144-entity-editor-epic-doc · PR: https://github.com/jakildev/IrredenEngine/pull/630
- [x] **T-109** — Migrate Lua perf-grid to CODEGEN, re-run parity gate, close #293 · Owner: claude/T-109-codegen-perf-grid · PR: https://github.com/jakildev/IrredenEngine/pull/599
- [x] **T-143** — Render: cache resolved sun direction once per frame · Owner: claude/T-143-resolve-sun-direction · PR: https://github.com/jakildev/IrredenEngine/pull/615
- [x] **T-108** — Per-system mode override + CODEGEN/EVAL coexistence · Owner: claude/T-108-mode-override-coexistence · PR: https://github.com/jakildev/IrredenEngine/pull/598
- [x] **T-141** — Demo: Z-Yaw world rotation showcase · Owner: claude/T-141-z-yaw-rotation-demo · PR: https://github.com/jakildev/IrredenEngine/pull/602
- [x] **T-142** — macOS — fix IRShapeDebug crash in UPDATE_VOXEL_SET_CHILDREN · Owner: claude/T-142-voxel-set-children-crash · PR: https://github.com/jakildev/IrredenEngine/pull/601
- [x] **T-140** — fleet: extract detect_engine_root into fleet-common.sh · Owner: claude/T-140-fleet-common-sh · PR: https://github.com/jakildev/IrredenEngine/pull/600
- [x] **T-107** — Codegen system bodies — DSL parser + C++ emitter · Owner: claude/T-107-codegen-system-bodies · PR: https://github.com/jakildev/IrredenEngine/pull/597
- [x] **T-106** — Codegen pipeline foundation — components only · Owner: claude/T-106-lua-codegen-foundation · PR: https://github.com/jakildev/IrredenEngine/pull/596
- [x] **T-105** — LuaJIT 2.1 runtime migration · Owner: claude/T-105-luajit-runtime · PR: https://github.com/jakildev/IrredenEngine/pull/595
- [x] **T-135** — fleet-up.conf concurrency cap · Owner: claude/T-135-fleet-up-conf-concurrency-cap · PR: https://github.com/jakildev/IrredenEngine/pull/594
- [x] **T-138** — fleet-claim: atomic master-side TASKS.md lock · Owner: claude/T-138-fleet-claim-master-lock · PR: https://github.com/jakildev/IrredenEngine/pull/593
- [x] **T-136** — Systems: registerSystem<N> helper to retire Params + setSystemParams boilerplate · Owner: claude/T-136-register-system-helper · PR: https://github.com/jakildev/IrredenEngine/pull/592
