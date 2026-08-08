## Plan: common/voxel: generic entity ground-anchor — EntityAnchor enum + GROUND mode on C_VoxelSetNew (position = center XY, bottom Z)

- **Issue:** #2563
- **Model:** opus — bounded after the survey below, but it touches component semantics, the serializer, and the Lua binding surface in one PR
- **Date:** 2026-08-04

### Verified current state (survey 2026-08-04 at `e4fa3f3ec`; re-verified at `6e804773e` — the only surveyed-path change in between is 4 lines of `test/script/lua_system_coexistence_test.cpp`, unrelated)

The issue's premise ("every placement consumer honors the anchor") is **stronger than the code requires**. `centerAroundOrigin` is not stored anywhere — it is a ctor-local parameter read exactly once, at `engine/prefabs/irreden/voxel/components/component_voxel_set.hpp:159-161`, where the offset `-(size-1)*0.5` per axis (or 0) is baked into `positions_[i].pos_`. Exhaustive grep: that line is the only read in the tree. Every downstream path derives placement from the baked `positions_` / `globalPositions_`:

- grid rebuild (all three arms): `system_rebuild_grid_voxels.hpp:249-255,293-299,316-530` via `worldCellForGridVoxel` (`grid_rotation.hpp:56-69`)
- translate-only child update: `component_voxel_set.hpp:502-524`
- GPU transform prepass: `system_update_voxel_positions_gpu.hpp:148-183` (`sqtToMat4` on the raw transform, locals carry the anchor)
- face occupancy: `face_occupancy.hpp:52-113` — pure index-space, anchor-invariant by construction
- cull extents: `component_voxel_pool.hpp:786-796,392-425` — expands live globals; the `-0.5` at `:392-399` is the per-cell anchor shift (#2545), not a per-set anchor
- picking: `picking.hpp:209-252,318-330` — derives origin from `globalPositions_[0]`, anchor-transparent

So GROUND needs **no changes on the rasterize/render/cull/occupancy/picking paths at all**. Half-integer local origins are already the norm (every even-size CENTER set has them — e.g. the 48×48×2 floors), so GROUND's half-integer z origin exercises no new rounding behavior on the grid path.

What the mechanism DOES need:

1. **Storage.** Because the mode is currently baked-and-discarded, nothing can observe it. Four sites hard-code or assume a convention and need the stored enum:
   - `system_voxel_squash_stretch.hpp:40,175,180-181` — hard-codes CORNER center `(size-1)*0.5` in local space (wrong for CENTER today).
   - `system_contact_note_burst.hpp:36-37` — hard-codes CORNER via `translation + size*0.5` (also a half-cell off the true box center `translation + (size-1)*0.5`).
   - **Census: both systems' gating components (`C_VoxelSquashStretch`, `C_ParticleBurst`) have zero attach sites in the tree** (grep over engine/, creations/, test/ — only the component/system/save-inventory files themselves match). Migrating them is observably free.
   - `system_rebuild_detached_voxels.hpp:41-44,101-108` — the detached path's half-extent measurement is `abs()` about the origin, correct only for CENTER; the header comment states the `centerAroundOrigin=true` invariant. `detached_revoxelize.hpp:83-97` asserts pool-wide anchor uniformity via `halfCellAnchor` (`grid_rotation.hpp:71-82`), which *detects* the CENTER residual rather than reading a mode.
   - `voxel_set_serialize.hpp:56-66` recovers `boundsMin` as `roundHalfUp(positions_[0].pos_)` into an `ivec3` — GROUND's z origin is always half-integer, so a GROUND set cannot round-trip through the current schema. (This recovery is already lossy for even-size CENTER sets — a pre-existing defect independent of this task; see Follow-ups.)
2. **Ctor census** (78 C++ construction sites + 3 Lua): explicit `true` 46, explicit `false` 17, omitted 11, dense-data 2, `StagedInit` 2. All engine-internal sites (6) use the default/no-bool forms. The Lua usertype binding (`component_voxel_set_lua.hpp:10-15`) already exposes the 3-arg bool overload; **no Lua site passes it** (all three demo scripts use the 2-arg form).
3. **Precedents.** No `EntityAnchor`-like enum exists. The canonical component-mode-enum pattern is `RotationMode` (`engine/prefabs/irreden/common/components/component_rotation_mode.hpp:56-80`): `enum class : uint8_t` with `kFirst`/`kLast` sentinels (required by `.claude/rules/cpp-lua-enums.md`'s range-check-then-cast read rule), Lua table minted via the `IR_BIND_*` stringized macro in `LuaScript::bindLuaDrivenEcs()` (`engine/script/src/lua_script.cpp:549-559`), validated reads per `prefab_api.cpp:152-183`.
4. **Rotation pivot property (free win):** `worldCellForGridVoxel` pivots about the local origin (`positions_ == 0`), so a GROUND set yaws about its footprint center at the ground plane — the desired semantic for a standing entity turning in place. CORNER spins about its corner, CENTER about its volumetric middle, exactly as today.

Sibling/in-flight reconciliation: #2564 (entity-anchored fog reveal, queued behind this issue) consumes exactly "the anchor IS the entity position" — this plan delivers that with no offsets exposed. #2557 (fog z-cost curve, in planning) touches fog shaders/fog_demo scenes; this plan's exemplar demo is `day_cycle`, so no file overlap. No open PR touches the voxel placement surface (checked 2026-08-04; #2475 is occlusion-cull-domain and parked, #2746 scopes the lua-enums rule *doc* only).

### Scope

One task, one PR. Adds the `EntityAnchor` enum + stored anchor on `C_VoxelSetNew` with GROUND mode, migrates the four convention-assuming sites, adds the Lua binding + one Lua spawn, flips one `day_cycle` cube as the C++ exemplar, and documents the convention. No other content flips; colliders/SDF shapes/`C_EntityCanvas` are out of scope (issue's own follow-up list).

### Approach

**Phase 0 — baseline controls (cheap, before any edit).** Run `IrredenEngineTest` (expect 1493/1494 — `SaveTrait.InventoryIsComplete` is the known pre-existing #2834 red; an empty `git diff` is the control for it) and `python3 scripts/render-verify.py --target IRShapeDebug` on the untouched tree. These are the identity gates phase 4 re-runs; capturing them first makes "still green, zero deltas" meaningful.

**Phase 1 — enum + storage + GROUND offset + unit test.**
- New header `engine/prefabs/irreden/common/components/entity_anchor.hpp`, namespace `IRComponents`:
  ```cpp
  enum class EntityAnchor : std::uint8_t {
      CORNER = 0,   // legacy default: geometry extends +x/+y/+z from translation
      CENTER = 1,   // legacy centerAroundOrigin: centered all 3 axes
      GROUND = 2,   // center XY, bottom Z (iso +Z is down: ground contact at translation.z)
      kFirst = CORNER,
      kLast  = GROUND,
  };
  constexpr vec3 anchorOffset(EntityAnchor a, ivec3 size);      // the baked local-origin offset
  constexpr vec3 anchorLocalCenter(EntityAnchor a, ivec3 size); // body center relative to translation
  ```
  Offsets: CORNER `(0,0,0)`; CENTER `-(s-1)*0.5` per axis; GROUND `(-(sx-1)*0.5, -(sy-1)*0.5, -(sz-0.5))`. GROUND cell faces then span `[-sx/2,+sx/2) × [-sy/2,+sy/2) × [-sz, 0)` relative to translation — the ground-contact face sits exactly at `translation.z`. `anchorLocalCenter = anchorOffset + (s-1)*0.5` per axis (CORNER: `(s-1)/2`, matching today's squash-stretch math; CENTER: `0`; GROUND: `(0,0,-sz*0.5)`). The header doc comment is the canonical convention text (issue item 4): new discrete-entity prefabs anchor GROUND; terrain-like/corner content stays CORNER.
  (Placement note: the issue says "engine common"; `engine/common/` has no component-semantics surface — only `ir_constants.hpp`/`ir_platform.hpp` — and the `RotationMode` precedent, the `IRComponents` namespace, and the Lua `IRComponent.*` table all live at the prefab-common layer, so that is the home.)
- `component_voxel_set.hpp`: add member `EntityAnchor anchor_ = EntityAnchor::CORNER;` + trivial accessor; new ctor overload `C_VoxelSetNew(ivec3 size, Color color, EntityAnchor anchor, EntityId targetCanvas = kNullEntity)` computing the offset via `anchorOffset()` and `IR_ASSERT`ing `kFirst <= anchor <= kLast` (the usertype ctor path bypasses `prefab_api`'s range check, so the ctor validates for both C++ and Lua callers); the existing bool ctor keeps its exact signature and delegates, mapping `false→CORNER`, `true→CENTER` (byte-identical offsets by construction — same expressions). `StagedInit`/dense ctors set `anchor_ = CORNER` (their origin is carried by boundsMin, unchanged).
- New `test/ecs/voxel_set_anchor_test.cpp` (+ registration in `test/CMakeLists.txt`, pattern per `test/ecs/voxel_set_edit_api_test.cpp`): GROUND size (2,2,2) asserts `positions_[0] == (-0.5,-0.5,-1.5)` and `positions_[7] == (+0.5,+0.5,-0.5)`; CORNER/CENTER positions byte-match the legacy formula for odd and even sizes; both bool-ctor arms map to the right enum; `anchor_` is stored; `anchorLocalCenter` values for all three modes.

**Phase 2 — consumer reconciliation (the cross-system audit's action arm).**
- `system_voxel_squash_stretch.hpp`: `blockCenter = anchorLocalCenter(voxelSet.anchor_, voxelSet.size_)` (also fixes the latent CENTER wrongness; zero in-tree consumers, see census).
- `system_contact_note_burst.hpp`: `blockCenter = worldXform.translation_ + anchorLocalCenter(...)` (also corrects the half-cell CORNER bias; zero in-tree consumers).
- `system_rebuild_detached_voxels.hpp`: guard the detached path — on encountering a set with `anchor_ == GROUND`, fire a debug assert/log naming the follow-up issue (GROUND×detached support is deferred; the half-extent math at `:101-108` and the `halfCellAnchor` uniformity assumption at `detached_revoxelize.hpp:88-96` are CENTER-shaped). Update the invariant comment at `:41-44` to name the enum instead of the bool.
- `voxel_set_serialize.hpp`: persist `anchor_` (schema-additive field, absent ⇒ CORNER on old data). On load, a non-CORNER record reconstructs its baked offset from `anchorOffset(anchor_, size_)` instead of trusting the `ivec3` boundsMin recovery (which cannot represent GROUND's half-integer z origin). Round-trip unit test: GROUND set save→load reproduces `positions_` and `anchor_` exactly (extend `test/world/voxel_set_serialize_test.cpp`).

**Phase 3 — Lua surface (per `.claude/rules/cpp-lua-enums.md`).**
- `engine/script/src/lua_script.cpp` (`bindLuaDrivenEcs()`, adjacent to the `IR_BIND_ROTMODE` block at `:549-559`): mint `IRComponent.EntityAnchor.{CORNER,CENTER,GROUND}` via the stringized `IR_BIND_*` macro — integers, no string names.
- `component_voxel_set_lua.hpp`: register the `(ivec3, Color, EntityAnchor)` ctor overload alongside the existing two.
- Script test (new or extended under `test/script/`): construct a GROUND set from Lua via `IRComponent.EntityAnchor.GROUND` and assert placement; ALSO construct via the legacy `(ivec3, Color, true)` bool form in the same suite — this pins sol2's boolean-vs-integer overload disambiguation, the one genuinely new binding risk.
- `creations/demos/default/main.lua:75-83`: flip the idle-batch factory's `C_VoxelSetNew.new` to the GROUND form — the issue's required in-tree Lua spawn. (1×1×1 sets shift by exactly (0,0,-0.5); the demo has no render-verify references.)

**Phase 4 — exemplar + docs + identity gates.**
- `creations/demos/day_cycle/main.cpp:244-251`: migrate ONE of the three 6×6×12 cubes to GROUND with `translation.z` set to the floor's top-surface z, replacing its hand-computed constant with the anchor semantics (floor: 48×48×2 CENTER at z=6 ⇒ cell faces span z∈[5,7], top face at z=5 ⇒ exemplar translation.z = 5, bottom face flush at 5). The other two cubes stay untouched as the visual control. Run `attach-screenshots` (target `IRDayCycle`) for the PR body.
- `engine/prefabs/irreden/voxel/CLAUDE.md`: one convention bullet pointing at the enum header.
- Re-run phase-0 gates: `IrredenEngineTest` (1493 pass + the new tests, #2834 still the only red) and render-verify with **zero reference updates** on every ref-bearing demo the host has references for (macOS: `IRShapeDebug` 21 shots, `IRFogDemo`, `IRLightingSdfBlocker`, `IRCanvasStress`; Linux: `IRCanvasStress` only). Neither flipped demo (`day_cycle`, `default`) has committed references, so the byte-identity gate and the exemplar change cannot contaminate each other.

### Affected files

- `engine/prefabs/irreden/common/components/entity_anchor.hpp` — **new**: enum + `kFirst`/`kLast` + `anchorOffset`/`anchorLocalCenter` + convention doc
- `engine/prefabs/irreden/voxel/components/component_voxel_set.hpp` — `anchor_` member, enum ctor, bool ctor delegation, offset via helper
- `engine/prefabs/irreden/voxel/components/component_voxel_set_lua.hpp` — register enum ctor overload
- `engine/prefabs/irreden/voxel/systems/system_voxel_squash_stretch.hpp` — anchor-aware center
- `engine/prefabs/irreden/update/systems/system_contact_note_burst.hpp` — anchor-aware center
- `engine/prefabs/irreden/voxel/systems/system_rebuild_detached_voxels.hpp` — GROUND guard + invariant comment
- `engine/prefabs/irreden/voxel/voxel_set_serialize.hpp` — persist + reconstruct anchor
- `engine/script/src/lua_script.cpp` — `IRComponent.EntityAnchor` table
- `test/ecs/voxel_set_anchor_test.cpp` — **new**; `test/CMakeLists.txt` — register it
- `test/script/` — Lua ctor/overload test (new file or extend an existing voxel-set script suite)
- `test/world/voxel_set_serialize_test.cpp` — GROUND round-trip case
- `creations/demos/day_cycle/main.cpp` — C++ exemplar flip (one cube)
- `creations/demos/default/main.lua` — Lua exemplar spawn
- `engine/prefabs/irreden/voxel/CLAUDE.md` — convention bullet
- `.fleet/plans/issue-2563.md` — this plan, as the PR's first commit (#1932)

### Acceptance criteria

1. **Positive-fire (unit):** `voxel_set_anchor_test.cpp` asserts the GROUND (2,2,2) baked positions `(-0.5,-0.5,-1.5)`/`(+0.5,+0.5,-0.5)`, the three `anchorLocalCenter` values, both bool-ctor mappings, and stored `anchor_` — new asserts that fire with the feature ON. Fixture is created by this plan (phase 1).
2. **Positive-fire (Lua):** the script test spawns a GROUND set via `IRComponent.EntityAnchor.GROUND` and asserts placement; the legacy bool form in the same suite still resolves to CENTER/CORNER.
3. **Positive-fire (serialize):** GROUND save→load round-trip reproduces `positions_` and `anchor_` exactly.
4. **Exemplar:** the migrated `day_cycle` cube's `translation.z == 5.0` (the floor's top-surface z) with its bottom face flush — before/after screenshot pair in the PR body; the two control cubes pixel-identical between the pair.
5. **Byte-identity:** `IrredenEngineTest` green except the pre-existing #2834 red; render-verify green with **zero** `--update-references` on every ref-bearing demo the host carries references for.
6. **Guard fires:** the detached-path GROUND guard observably fires in a test (or, minimum, a hand-run with a GROUND set on a detached canvas shows the assert/log) — with a positive control showing a CENTER set passes the same path silently.

### Gotchas

- **GROUND's z origin is half-integer for every size** (unlike CENTER, where it depends on parity). The grid path already handles half-integer locals ubiquitously, but the detached path *detects* CENTER via `halfCellAnchor` residuals and *measures* extents `abs()`-about-origin — that is why phase 2 guards it instead of supporting it. Do not attempt GROUND-on-detached in this PR.
- **Do not "fix" the even-size CENTER serialize lossiness here** (`voxel_set_serialize.hpp:56-66` + its "round-trip exactly" comment) — pre-existing, independent; verify and file it separately (see Follow-ups).
- The sol2 ctor overload set will hold `(ivec3,Color,bool)` and `(ivec3,Color,EntityAnchor)` — a Lua boolean must keep hitting the bool arm and a Lua integer the enum arm; criterion 2's dual-form test is the pin. If sol2's resolution proves ambiguous, the fallback within this plan is dropping the Lua bool arm registration (no in-tree Lua caller uses it — census above) and keeping only 2-arg + enum forms; C++ back-compat is unaffected.
- `IrredenEngineTest` on master is 1493/1494 — `SaveTrait.InventoryIsComplete` is #2834, never this PR's; operands outside the diff ⇒ the phase-0 empty-diff run is the control.
- render-verify on macOS takes ~35 min across the ref-bearing demos — slow, not hung. The screenshots dir accumulates across runs; stale shots fake regressions — clear between runs.
- New-file registration: the test suite registers in `test/CMakeLists.txt`; miss it and ctest silently runs without the new suite.
- Plans are engine-public: keep all naming engine-side (this plan and the PR body).

### Cross-system audit (shared-convention migration)

| Consumer | Site | Disposition |
|---|---|---|
| grid rebuild (3 arms) / child update / GPU prepass / occupancy / cull / picking | survey list above | anchor-transparent via baked `positions_` — **no change** |
| `VOXEL_SQUASH_STRETCH` | `system_voxel_squash_stretch.hpp:40,175,180-181` | migrate to `anchorLocalCenter` (phase 2; zero attach sites) |
| `CONTACT_NOTE_BURST` | `system_contact_note_burst.hpp:36-37` | migrate to `anchorLocalCenter` (phase 2; zero attach sites) |
| `PERIODIC_IDLE_NOTE_BURST` (the other `C_ParticleBurst` consumer) | `system_periodic_idle_note_burst.hpp:42` | spawns at raw `translation_`, no size math — consumes the anchor semantics by construction, **no change** |
| detached rebuild + revoxelize | `system_rebuild_detached_voxels.hpp:41-44,101-108`; `detached_revoxelize.hpp:83-97` | guard GROUND + comment (phase 2); support deferred |
| serializer | `voxel_set_serialize.hpp:56-66,148` | persist anchor, reconstruct offset (phase 2) |
| Lua ctor surface | `component_voxel_set_lua.hpp:10-15` | add enum overload (phase 3) |
| in-tree ctor sites (78 C++ / 3 Lua) | census above | untouched — bool forms map losslessly; only the two named exemplars flip |

### Follow-ups to file (out of scope, during implementation)

- Even-size CENTER serialize round-trip lossiness (pre-existing; verify with a failing-at-head repro test before filing).
- GROUND support on the detached-canvas path (extent measurement + `halfCellAnchor` generalization) — the phase-2 guard names this issue once filed.
- GROUND interpretation for `C_ColliderIso3DAABB` / SDF shapes / `C_EntityCanvas` (already listed by the issue).
