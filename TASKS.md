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


- [~] **editor: loft from 2 profiles (CSG of two extrusions) (A2)** — author front (XZ) and side (YZ) silhouettes on 2D mask overlay; voxels placed where both masks intersect
  - **ID:** T-285
  - **Area:** engine/prefabs/irreden/editor
  - **Model:** sonnet
  - **Owner:** claude/T-285-editor-loft-profiles
  - **Blocked by:** (none)
  - **Stack:** T-284..T-286 S-A-author
  - **Acceptance:** (1) Author a sphere-like shape from two circle profiles; (2) author a chair-like shape from front + side silhouettes; (3) mask widgets snap to grid; modifier key for symmetry plane; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #948
  - **Notes:** Stack S-A-author pos 3. Branch from A4 PR head. Mask widget reuses trixel-rect helpers. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic A → A2".
  - **Links:**

- [~] **render: wire detached-canvas rotation through composite TRS (C3)** — thread C_LocalTransform through per-canvas TRS for DETACHED entities at system_entity_canvas_to_framebuffer.hpp:98-100; support both voxel and SDF entities
  - **ID:** T-291
  - **Area:** engine/render, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** claude/T-291-detached-canvas-rotation
  - **Blocked by:** (none)
  - **Stack:** T-279..T-295 S-C-core
  - **Acceptance:** (1) A DETACHED rectangular entity spins smoothly around its local Z axis without voxel re-rasterization; (2) perf_grid shows constant per-frame cost regardless of rotation rate; (3) works for both voxel and SDF entities; (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #954
  - **Notes:** Stack S-C-core pos 3. Branch from C2 PR head. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic C → C3".
  - **Links:**

- [~] **voxel: GRID-mode rotation re-rasterizes voxels on transform change (C6)** — SYSTEM_REBUILD_GRID_VOXELS runs on entities with changed C_LocalTransform; rotates authored voxels to world-grid cells; last-writer-wins on cell collisions (deterministic by entity ID)
  - **ID:** T-294
  - **Area:** engine/prefabs/irreden/voxel, engine/render
  - **Model:** opus
  - **Owner:** claude/T-294-grid-mode-rotation
  - **Blocked by:** T-291
  - **Acceptance:** (1) A cube rotated 45° around Z occupies a different set of world voxel cells than at 0°; (2) rotation snaps to grid (aliasing accepted by design — documented in the system header); (3) deterministic across frames; cell collisions documented; (4) interacts cleanly with Epic E E5 (entity chunk migration) for rotated entities crossing chunk boundaries; (5) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #957
  - **Notes:** Off-stack fork from C3; does NOT block S-C-core's C3 → C7 chain. Branch from C3 PR head. Push-at-mutation; no dirty flag per cpp-ecs. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic C → C6".
  - **Links:**

- [~] **render: DETACHED canvas pitch/roll (full SO(3) inside canvas) (C7)** — detached canvases support full SO(3) local rotation via per-face deformation math applied in entity's local frame; world composite applies world Z-yaw deformation on top (composition order: local first, then world)
  - **ID:** T-295
  - **Area:** engine/render, shaders/glsl
  - **Model:** opus
  - **Owner:** claude/T-295-detached-canvas-so3
  - **Blocked by:** T-291
  - **Stack:** T-279..T-295 S-C-core
  - **Acceptance:** (1) A DETACHED rectangular entity pitching forward looks correct from any world Z-yaw; (2) deformation math reused (single source of truth across world and per-canvas); (3) composition order correct (local rotation applied first inside canvas, then world Z-yaw at composite); (4) fleet-build clean on linux-debug and macos-debug
  - **Issue:** #958
  - **Notes:** Stack S-C-core pos 4. Branch from C3 PR head; also requires C4 (T-293) merged first. Plan ref: `.claude/plans/okay-lets-go-through-idempotent-giraffe.md` §"Epic C → C7".
  - **Links:**

- [ ] **render: migrate Phase A render-side readers to C_WorldTransform** — migrate ~11 render-pipeline systems from C_PositionGlobal3D/C_Rotation to C_WorldTransform (pure reader migration, no writer changes in this phase)
  - **ID:** T-299
  - **Area:** engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-300
  - **Stack:** T-299..T-302 sqt-phase-a
  - **Acceptance:** (1) Each listed render system reads C_WorldTransform.translation_/.rotation_ instead of C_PositionGlobal3D/C_Rotation; (2) legacy components still exist (retirement is T-302); (3) fleet-build clean on linux-debug and macos-debug; (4) render-debug-loop shot list pre/post pixel-identical (within bilinear tolerance for rotation-touching shaders); (5) voxel editor renders and interacts correctly — gizmos move, picking resolves, voxels position correctly
  - **Issue:** #984
  - **Notes:** Step 2 of transform migration decomposed from #736. Foundation already merged: PR #749/T-197 (C_LocalTransform + C_WorldTransform + SYSTEM_PROPAGATE_TRANSFORM), PR #787/T-199-step-1 (COMPUTE_LIGHT_VOLUME proof-of-concept). Target files: system_sprites_to_screen.hpp, system_entity_canvas_to_framebuffer.hpp, system_shapes_to_trixel.hpp, system_voxel_picking.hpp, system_voxel_to_trixel.hpp, system_gizmo_drag.hpp, system_gizmo_screen_space_size.hpp, system_build_light_occlusion_grid.hpp, system_debug_culling_minimap.hpp, picking.hpp, gizmo.hpp. Pure mechanical swap: C_PositionGlobal3D pos → C_WorldTransform xform, pos.pos_ → xform.translation_; C_Rotation → xform.rotation_. Architect inverted the original chain on PR #1000 (2026-05-19) — T-300 (writers + PROPAGATE_TRANSFORM wiring) must land first; only after that does the mechanical reader swap render correctly across the 14 creations that don't yet have PROPAGATE_TRANSFORM wired. See PR #1000 architect-direction comment for the full rationale. Unblocks when T-300 merges; parallel to T-301.
  - **Links:**

- [ ] **update: migrate Phase A update-side consumers to C_WorldTransform** — migrate ~15 update-pipeline systems and entity-spawn helpers from C_PositionGlobal3D/C_Position3D/C_Rotation to C_LocalTransform/C_WorldTransform; writers convert Euler C_Rotation to quat C_LocalTransform.rotation_
  - **ID:** T-300
  - **Area:** engine/prefabs/irreden/update, engine/prefabs/irreden/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** (none)
  - **Stack:** T-299..T-302 sqt-phase-a
  - **Acceptance:** (1) Each listed update system reads C_WorldTransform.translation_/.rotation_ and/or writes C_LocalTransform; (2) velocity integration, spring physics, periodic offsets, goto/reactive-return all behave identically post-migration; (3) fleet-build clean on both backends; (4) unit tests pass (test/ecs/*, test/asset/*); (5) IRShapeDebug + voxel editor + spring/note demos render identically; (6) SYSTEM_PROPAGATE_TRANSFORM runs in every UPDATE pipeline that has a transform-consuming entity (architect-added scope per PR #1000 — engine-default registration preferred over per-creation if structurally viable)
  - **Issue:** #985
  - **Notes:** T-199b — update-side continuation of #736 migration. Architect reordered the chain on PR #1000 (2026-05-19): T-300 now stacks on master directly, T-299 is downstream. Rationale: of 19 creations using shape/sprite/voxel/canvas/gizmo components, only 5 have PROPAGATE_TRANSFORM wired (and only for C_LightSource per T-199 step 1) — migrating readers first would render every shape/sprite/voxel/gizmo at world origin in the other 14 creations because nothing populates C_WorldTransform from the legacy C_PositionGlobal3D channel. Target readers: system_periodic_idle_position_offset.hpp, system_apply_position_offset.hpp, system_rhythmic_launch.hpp, system_contact_note_burst.hpp, system_collision_note_platform.hpp, system_update_positions_global.hpp, system_periodic_idle_note_burst.hpp, system_particle_spawner.hpp, system_velocity.hpp, system_spring_platform.hpp, system_action_animation.hpp, system_reactive_return_3d.hpp, system_goto_3d.hpp, system_update_screen_view.hpp. Writers need Euler→quat: use glm::quat(glm::vec3(eulerAngles)) for Euler angles; glm::quat_cast(rotMat) only if working from a rotation matrix. system_apply_position_offset.hpp may be deleted if epic #731 modifier migration has removed it — check current state first. Architect added PROPAGATE_TRANSFORM wiring scope: prefer engine-default registration; document why and fall back to per-creation if Option A is structurally infeasible. Blocks T-299 → T-301 → T-302.
  - **Links:**

- [ ] **voxel: migrate voxel-side to C_WorldTransform — C_VoxelPool SoA design decision** — migrate C_VoxelPool and ~10 voxel-pipeline files off legacy position components; architect call required on pool SoA layout (Option A: C_WorldTransform array vs Option B: position-only projection arrays)
  - **ID:** T-301
  - **Area:** engine/prefabs/irreden/voxel, engine/render
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-300
  - **Stack:** T-299..T-302 sqt-phase-a
  - **Acceptance:** (1) C_VoxelPool and voxel systems use new SQT shape per the architectural decision; (2) GPU voxel-position upload path still works — voxel rendering pixel-identical pre/post; (3) voxel editor place/erase + scene save/load round-trip unchanged; (4) fleet-build clean on both backends; (5) engine/prefabs/irreden/voxel/CLAUDE.md updated with SoA decision rationale
  - **Issue:** #986
  - **Notes:** T-199c — voxel-side continuation; trickiest phase because C_VoxelPool SoA arrays back the GPU buffer upload path. Option A: one C_WorldTransform array per pool (wider per-voxel memory but uniform with rest of engine post-migration); Option B: keep position-only projection arrays refreshed from owning entity's C_WorldTransform once per pool tick (smaller; preserves current GPU upload contract). Worker should escalate via fleet:design-blocked when reaching the C_VoxelPool rewrite — architect input needed. Stacks on T-300. Blocks T-302.
  - **Links:**

- [ ] **engine: retire C_Position3D / C_PositionGlobal3D / C_Rotation legacy components** — delete all four legacy position/rotation component headers; remove from createEntity auto-attach set; migrate Lua bindings and script API to SQT equivalents; update CLAUDE.md
  - **ID:** T-302
  - **Area:** engine/prefabs/irreden/common, engine/script
  - **Model:** opus
  - **Owner:** free
  - **Blocked by:** T-301
  - **Stack:** T-299..T-302 sqt-phase-a
  - **Acceptance:** (1) grep -r "C_PositionGlobal3D" engine/ returns zero hits; (2) grep -r "C_Position3D" engine/ returns zero hits; (3) grep -r "C_Rotation" engine/ returns zero hits; (4) createEntity no longer auto-attaches legacy components; (5) fleet-build clean on linux-debug and macos-debug; (6) full test suite passes (test/ecs/*, test/asset/*, render-verify reference set); (7) IRShapeDebug, voxel editor, spring/note demos render identically; (8) CLAUDE.md retirement note added
  - **Issue:** #987
  - **Notes:** T-199d — deletion phase, closes the #736 migration chain. Deletions: component_position_3d.hpp + _lua.hpp, component_position_global_3d.hpp, component_position_offset_3d.hpp (if not already deleted by epic #731 modifier migration), component_rotation.hpp. Watch for creations/ consumers missed by engine audit — build fails loudly. Lua migrations: engine/script/src/prefab_api.cpp and lua_sprite_namespace.hpp. This closes a multi-phase migration — diff should be mostly red. Stacks on T-301.
  - **Links:**

## Done — last 20

<!-- Completed tasks, newest first. Prune older entries beyond 20. -->

- [x] **T-298** — world: chunk disk persistence + lazy load (E6) · Owner: claude/T-298-chunk-disk-persistence · PR: https://github.com/jakildev/IrredenEngine/pull/998
- [x] **T-293** — render geometric trixel deformation (replaces T-322 bilinear residual) · Owner: claude/T-293-geometric-trixel-deformation · PR: https://github.com/jakildev/IrredenEngine/pull/1005
- [x] **T-290** — C_RotationMode enum + component (C2) · Owner: claude/T-290-rotation-mode-component · PR: https://github.com/jakildev/IrredenEngine/pull/1001
- [x] **T-289** — voxel: push-at-mutation position upload (no per-frame re-upload) (B5) · Owner: claude/T-289-voxel-pos-push-at-mutation · PR: https://github.com/jakildev/IrredenEngine/pull/999
- [x] **T-284** — editor selection rectangle + ghost preview during fill (A4) · Owner: claude/T-284-fill-ghost-ui · PR: https://github.com/jakildev/IrredenEngine/pull/989
- [x] **T-292** — math: continuous-yaw + deformation math helpers (C5) · Owner: claude/T-292-yaw-deformation-math · PR: https://github.com/jakildev/IrredenEngine/pull/1002
- [x] **T-297** — world: chunk container + ivec3 chunk-coords addressing (E1) · Owner: claude/T-297-world-chunk-container · PR: https://github.com/jakildev/IrredenEngine/pull/997
- [x] **T-296** — docs: lock SDF restriction decision (D2) · Owner: claude/T-296-sdf-restriction-decision · PR: https://github.com/jakildev/IrredenEngine/pull/996
- [x] **T-288** — voxel: face-aware shader skip + per-voxel face-occlusion bits (B2) · Owner: claude/T-288-voxel-face-occupancy · PR: https://github.com/jakildev/IrredenEngine/pull/994
- [x] **T-286** — editor parametric-shape voxel bake (always DENSE) (A3) · Owner: claude/T-286-parametric-voxel-bake · PR: https://github.com/jakildev/IrredenEngine/pull/993
- [x] **T-287** — voxel: sparse occupancy bitmask in C_VoxelPool (B1) · Owner: claude/T-287-voxel-active-mask · PR: https://github.com/jakildev/IrredenEngine/pull/988
- [x] **T-277** — render: runtime-sized voxel pools (B4) · Owner: claude/T-277-runtime-voxel-pools · PR: https://github.com/jakildev/IrredenEngine/pull/975
- [x] **T-276** — asset: .vxs DENSE-RLE chunk variant (B3) · Owner: claude/T-276-vxs-rle-chunk · PR: https://github.com/jakildev/IrredenEngine/pull/972
- [x] **T-281** — render: C_ShapeDescriptor usage audit + docs/design/sdf-runtime-audit.md (D1) · Owner: claude/T-281-sdf-runtime-audit · PR: https://github.com/jakildev/IrredenEngine/pull/982
- [x] **T-280** — world streaming design doc (E0) · Owner: claude/T-280-world-streaming-design · PR: https://github.com/jakildev/IrredenEngine/pull/981
- [x] **T-283** — fleet: filter fleet:epic in project_queue_manager_ingest · Owner: claude/T-283-epic-filter-projector · PR: https://github.com/jakildev/IrredenEngine/pull/980
- [x] **T-282** — fleet: invalidate seen-hash on ingest lock-bail · Owner: claude/T-282-ingest-lock-bail-hash-invalidate · PR: https://github.com/jakildev/IrredenEngine/pull/978
- [x] **T-275** — render IRProfile ScopeTimer + per-stage CPU timing (B0) · Owner: claude/T-275-profile-scope-timer · PR: https://github.com/jakildev/IrredenEngine/pull/977
- [x] **T-278** — editor AABB box-fill + line-fill + face-fill (A1) · Owner: claude/T-278-fill-tools · PR: https://github.com/jakildev/IrredenEngine/pull/976
- [x] **T-215** — editor F-1.5 — save/load round-trip with metadata + JSON sidecar · Owner: claude/T-215-save-load-roundtrip · PR: https://github.com/jakildev/IrredenEngine/pull/933
