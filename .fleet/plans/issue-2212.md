# Plan: persist P1 — SaveTrait<C> + kSaveVersion (W-1, W-2)

- **Issue:** #2212
- **Model:** sonnet
- **Date:** 2026-07-03
- **Epic:** #667 — see `.fleet/plans/issue-667.md` for full context
- **Blocked by:** (none)

### Scope

Deliver the per-component persistence policy layer that every later persist phase consumes: a compile-time `SaveTrait<C>` trait (opt-in / opt-out) plus a `static constexpr uint32_t` save version for each opted-in component, and an explicit, audited decision stamped onto the entire engine component inventory (169 `C_*` types). No archetype walk, no IRWS writer, no serialize/deserialize functions, no Lua surface — those are P2+ (W-3..W-11). P1 is pure metadata + a build-time completeness gate.

In scope:
- `engine/world/save_trait.hpp` — the trait mechanism (opt-in/opt-out template, `kSaveVersion`, `shouldSave<C>()` / `saveVersion<C>()` helpers, `IR_SAVE_OPT_IN` / `IR_SAVE_OPT_OUT` macros).
- A component-inventory header enumerating every engine component with its explicit decision, plus the compile-time fold that fails the build if any listed component lacks a decision.
- A GoogleTest under `test/world/` that spot-checks the classification and backstops inventory completeness.

Out of scope (do not build): the `ComponentId → serialize descriptor` runtime bridge (W-3), the CMPN name-table wiring (W-3/W-5), migration registry (W-4), GPU-handle regeneration pass (W-10). P1 only makes the decisions those phases read.

### Verified current state

- **#663 primitives landed in `engine/asset/`** (headers read directly): `binary_io.hpp` — abstract `IRAsset::BinaryWriter` / `BinaryReader` with `File*`/`Memory*` backends; `writeU8/16/32/64`, `writeI*`, `writeF32/F64`, `writeVarUInt(uint64)`, `writeString`, `writeTag`, `tell()/seek()`; reads return `Result<T>{ BinaryStatus status_; T value_; }`; errors via `BinaryIOError` enum, never throw (`binary_io.hpp:31-87`, `:95-153`, `:208-251`). `chunk_header.hpp` — `AssetHeader` (`kAssetHeaderSize=12`); `writeChunked`, `readChunks`, `findChunk`, `makeTag` (`chunk_header.hpp:33-121`). `name_table.hpp` — `writeNameTable`/`readNameTable` + bidirectional `NameTable` — the Rule #2 vehicle W-3 will use for CMPN (`name_table.hpp:38-85`). `json_sidecar.hpp`, `math_binary_io.hpp` also present. Existing `kSaveVersion` precedent is **`uint16_t`** on asset records (`rig_format.hpp:73,86`; `voxel_set_format.hpp:216,250`), guarded by the `// IRAsset: serialized` simplify check — conflicts with the epic's `uint32_t`; reconciled under Approach/Gotchas.
- **`engine/world/` shape**: `world.cpp`, `chunk_residency.{hpp,cpp}`, `chunk_persistence.{hpp,cpp}`, `config.hpp`. `save_trait.hpp` is a **new sibling** header. `engine/world/CMakeLists.txt` **already links `IrredenEngineAsset`** — no CMake change needed.
- **Component registration is lazy and type-erased**: `EntityManager::registerComponent<C>()` keys on `typeid(C).name()` (mangled) and stores an `IComponentDataImpl<C>` (`entity_manager.hpp:165-181`, `:226-232`; `i_component_data.hpp:49-93`). There is **no central list of engine components** anywhere — no X-macro, no registration table, purely first-use lazy. Consequence: (a) P1 must **create** the canonical component list itself, and (b) mangled `typeid` names are unstable across compilers, so the disk-stable component name must come from another source — the P1 inventory X-list stringizes the type token (`"C_Velocity3D"`) and gives W-3 a stable name for free.
- **`ResourceId = uint32_t`** (`ir_render_types.hpp:17`); GPU components hold `std::pair<ResourceId, T*>` and free via `IRRender::destroyResource<T>` in `onDestroy()`.
- **Test harness**: single GoogleTest exe `IrredenEngineTest`, sources in `test/CMakeLists.txt:4+`; `test/world/` exists.
- **Inventory size**: 169 unique `struct C_*` across `engine/prefabs/**`, `engine/render/`, `engine/system/`.

### Approach

Single committed approach: **trait mechanism header + policy/inventory header + compile-time fold + backstop test.** Decisions live in one reviewable place, component structs stay untouched, and the build fails if any listed component has no decision.

**Step 1 — `engine/world/include/irreden/world/save_trait.hpp` (mechanism).** Lightweight, no component includes.
- Primary template encodes "no decision yet": `template <class C> struct SaveTrait { static constexpr bool kExplicit = false; static constexpr bool kSave = false; static constexpr std::uint32_t kSaveVersion = 0; };`
- Two policy macros that specialize it, forcing `kExplicit = true`: `IR_SAVE_OPT_IN(Type, Version)` (version `>= 1`, `static_assert`ed) and `IR_SAVE_OPT_OUT(Type)`.
- `constexpr bool shouldSave<C>()` / `constexpr std::uint32_t saveVersion<C>()` accessors.
- **`kSaveVersion` is `uint32_t` and lives in the trait, not on the component struct.** The world-snapshot serializes a *schema* defined by the (P2) per-component serialize function, not the struct's in-memory layout; the schema's version belongs beside the policy. Also sidesteps the asset `uint16_t` simplify regex. Honors the epic's locked `uint32_t` decision. (See Gotchas for the version-bump-detection follow-up this creates.)

**Step 2 — `engine/world/include/irreden/world/save_component_inventory.hpp` (policy + completeness gate).** Includes every component header (heavy — only pulled by world-snapshot TUs + the test), then: one `IR_SAVE_OPT_IN`/`IR_SAVE_OPT_OUT` line per component (the audit table below, transcribed into code); a canonical `using AllEngineComponents = std::tuple<...>;` listing all 169 types; and the compile-time completeness gate — `static_assert(detail::allExplicit<AllEngineComponents>(), "Every engine component must have an explicit IR_SAVE_OPT_IN/OPT_OUT decision.")`. Any listed component without a specialization uses the primary template (`kExplicit=false`) and **breaks the build**.

**Step 3 — `test/world/save_trait_test.cpp` + register in `test/CMakeLists.txt`.** Spot-checks the safety-critical opt-outs and representative opt-ins; fold invariants (`shouldSave ⇒ version >= 1`; `!shouldSave ⇒ version == 0`); and the **completeness backstop**: `EXPECT_EQ(std::tuple_size_v<AllEngineComponents>, kExpectedEngineComponentCount)` (169) — adding a component without an inventory entry fails this test. Pair with a note in `engine/world/CLAUDE.md` and a grep sanity command in the test comment.

**Step 4 — docs.** "Save-trait policy layer (persist P1)" section in `engine/world/CLAUDE.md`: where decisions live, opt-out-by-omission-is-forbidden, the "new component ⇒ add an inventory entry" contract.

### Affected files

New: `engine/world/include/irreden/world/save_trait.hpp`; `engine/world/include/irreden/world/save_component_inventory.hpp`; `test/world/save_trait_test.cpp`.
Edited: `test/CMakeLists.txt`; `engine/world/CLAUDE.md`.
Untouched deliberately: all 169 component headers; `engine/world/CMakeLists.txt`; `engine/asset/**`.

### Cross-system audit (component inventory)

Method: GPU-handle / `ResourceId` bearers and every ambiguous case **verified by reading the header** (per the #1814 lesson). Plain-data types classified by field shape + domain, confirmed line-by-line as the inventory file is written; the compile-time gate guarantees none is silently skipped.

**Class A — GPU-handle / ResourceId-owning → OPT-OUT** (process-local handles; W-10 regenerates post-load), all verified: `C_TrixelCanvasFramebuffer` (`component_trixel_framebuffer.hpp:17,29`), `C_TriangleCanvasTextures` (3× texture pair + `hiZMips_`, `:86-89`), `C_CanvasAOTexture` (`:20,34`), `C_CanvasFogOfWar` (`:123,162`), `C_CanvasSunShadow` (`:22,36`), `C_CanvasLightVolume` (2× Texture3D, `:55-56,78-80`), `C_PerAxisTrixelCanvases` (`:60-64,78,126-129`), `C_GPUParticlePool` (`:79-80,182`), `C_StatelessParticleEmitters` (`:59-61,115-116`), `C_DetachedRevoxelizeBuffer` (`:44,61,82-88`), `C_SpriteSheet` (**owns** `textureHandle_`, frees in `onDestroy()`, `:54,72-74`), `C_Sprite` (references, does not free). Atlas reloads from the `.irsprite`/PNG asset, not the snapshot.

**Class B — derived / rebuildable → OPT-OUT**: `C_VoxelPool` (canvas-scoped CPU mirror + free-span allocator; no ResourceId — verified; fully derived, lives on the `C_Persistent` canvas entity, `component_voxel_pool.hpp:676-726`), `C_SpatialIndex`, `C_RenderCache`, `C_ActiveLodLevel`, `C_ChunkVisibleThisFrame`, `C_FrameDataTrixelToFramebuffer`, `C_CanvasLocalRotation`, `C_LayoutState`, `C_LayoutLeaf`, `C_ResolvedFields`.

**Class C — transient per-frame events / device input → OPT-OUT**: `C_ContactEvent`, `C_OverlapContactBatch`; input snapshots `C_CursorPosition`, `C_MousePosition`, `C_MouseScroll`, `C_KeyboardKey`, `C_KeyStatus`, `C_KeyPressed`, `C_KeyReleased`, `C_KeyMouseButton`, `C_GLFWGamepadState`, `C_GLFWJoystick`, `C_IsGamepad`; transient MIDI I/O `C_MidiMessage`, `C_MidiMessageData`, `C_MidiMessageStatus`, `C_MidiIn`, `C_MidiOut`.

**Class D — engine/ECS-internal plumbing → OPT-OUT** (the archetype walk excludes system/relation/component-backing entities): `C_SystemEvent<…>`, `C_SystemRelation`, `C_IsNotPure`, `C_MarkedForDeletion`, `C_NoGlobalModifiers`, `C_GlobalModifiers`. `C_LambdaModifiers` — **hard** opt-out (`std::function`). `C_Modifiers` (+ vec3/quat variants) and `C_ResolvedFields` — OPT-OUT for v1 (source `EntityId`s + transient resolved fields, `component_modifiers.hpp:44-109,174-180`); revisit once entity-id remap + resolution ordering settle in W-3.

**Class E — ambiguous, explicit calls with rationale:**
- `C_VoxelSetNew` → **OPT-IN, flagged provisional**: authored voxel data is gameplay state, but the payload lives in the pool span + `pendingVoxels_` and load must re-allocate + round-trip `canvasEntity_`/`ownerEntityId_` EntityIds (`component_voxel_set.hpp:24-102,294`). Its serializer is custom and P2/W-3+, interacting with W-10 (P6 defines the staged-mode contract). If W-3's slice can't absorb it, flipping to OPT-OUT is a one-line inventory edit — by design.
- `C_Skeleton` → **OPT-IN**: `joints_` (EntityId vector) + `bindPose_` are authored rig topology; EntityIds round-trip under the snapshot's id-stable contract (`component_skeleton.hpp:71-73`).
- `C_JointHierarchy` → **OPT-OUT**: deprecated compile shim (superseded by `C_Skeleton`).

**Class F — plain gameplay data → OPT-IN, kSaveVersion = 1** (each gets its own inventory line): transform/spatial (`C_LocalTransform`, `C_WorldTransform`, `C_Position2D`, `C_Position2DIso`, `C_PositionInt2D`, `C_PositionInt3D`, `C_SizeInt2D`, `C_SizeInt3D`, `C_SizeTriangles`, `C_Direction3D`, `C_Magnitude`, `C_RotationMode`, `C_RotationTarget`, `C_AutoSpin`, `C_ChunkMembership`); physics/motion (`C_Velocity3D`, `C_Velocity2DIso`, `C_VelocityDrag`, `C_Acceleration3D`, `C_Gravity3D`, `C_HasGravity`, `C_GotoEasing3D`, `C_ReactiveReturn3D`, `C_SpringPlatform`, `C_WallBounce`, `C_WallDeath`); colliders/hitboxes (`C_ColliderIso3DAABB`, `C_CollisionLayer`, `C_HitBox2D`, `C_HitBox2DGui`, `C_HitboxCircle`, `C_HitboxRect`, `C_ActiveHitbox`); lifetime/timers/sim (`C_Lifetime`, `C_Timer`, `C_Stopwatch`, `C_Cycle`, `C_Loop`, `C_Alarm`, `C_PeriodicIdle`, `C_SimClock` singleton); animation (`C_AnimationClip`, `C_ActionAnimation`, `C_AnimClipColorTrack`, `C_AnimColorState`, `C_AnimMotionColorShift`, `C_ProceduralAnimation`, `C_LerpEntity`, `C_VoxelSquashStretch`, `C_SpriteAnimation`, `C_TextureScrollPosition`, `C_TextureScrollVelocity`); effects params (`C_ParticleBurst`, `C_ParticleSpawner`, `C_SpawnGlow`, `C_TriggerGlow`, `C_RhythmicLaunch`); appearance/shape (`C_ColorHSV`, `C_GeometricShape`, `C_ShapeDescriptor`, `C_TriangleCanvasBackground`, `C_TrianglesOnlySet`, `C_LightSource`, `C_LightBlocker`); camera/viewport/canvas config (`C_Camera`, `C_CameraPosition2DIso`, `C_Viewport`, `C_ZoomLevel`, `C_TrixelCanvasOrigin`, `C_TrixelCanvasRenderBehavior`, `C_EntityCanvas`, `C_CanvasTarget`, `C_DetachedCanvas`, `C_MainCanvas`, `C_GuiCanvas`, `C_BackgroundCanvas`); voxel/rig authored (`C_Voxel`, `C_Joint`, `C_JointName`, `C_BindPoints`); authored MIDI (`C_MidiSequence`, `C_MidiNote`, `C_MidiChannel`, `C_MidiDevice`, `C_MidiDelay`, `C_MidiSourcePort`); text/GUI/editor (`C_TextSegment`, `C_TextStyle`, `C_GuiPosition`, `C_GuiElement`, `C_GuiHoverState` singleton, `C_Widget` + the 11 `C_Widget*` kinds, `C_WidgetState`, `C_Splitter`, `C_VoxelSelection`, `C_VoxelSelectionHighlight`, `C_GizmoHandle`); tags/markers as explicit opt-in identity (`C_Player`, `C_ControllableUnit`, `C_Selected`, `C_Persistent`, `C_Name`, `C_Example`, `C_HelpText`, `C_SpatialQueryable`, `C_EntityHoverDetectTag`, `C_PerfStatsOverlayTag`).

Every one of the 169 names maps to exactly one class; the inventory file is that mapping in code.

### Acceptance criteria

1. **Compile-time completeness gate fires.** The inventory compiles clean; deleting any single `IR_SAVE_*` line makes `fleet-build --target IrredenEngineTest` fail with the "must have an explicit … decision" message.
2. **Runtime spot-checks pass** (`IrredenEngineTest --gtest_filter=SaveTrait.*`): every Class A bearer → `shouldSave() == false`; `C_LambdaModifiers`/`C_ContactEvent`/`C_SpatialIndex`/`C_VoxelPool` → `false`; representative Class F (`C_Velocity3D`, `C_LocalTransform`, `C_Name`, `C_ShapeDescriptor`, `C_SimClock`) → `true` with `saveVersion() >= 1`.
3. **Fold invariants** hold for every component (compile-time fold + runtime mirror).
4. **Completeness backstop**: tuple size == documented count (169).
5. Compiles + tests green on `linux-debug` and `macos-debug` (epic acceptance #7).
6. **No behavioral change** — header-only additions + one test.

### Gotchas

- **`uint32_t` vs the asset `uint16_t kSaveVersion` convention.** Trait-carried `uint32_t` avoids the collision, but creates a coverage gap: the field-layout version-bump detector watches annotated structs, and trait-carried versions are invisible to it. Mitigation: document the bump rule in `engine/world/CLAUDE.md`; file a follow-up to teach `simplify` a version-bump warning keyed on opt-in component edits. Do not attempt the simplify change in P1.
- **Heavy include in the inventory header** — pull it only into world-snapshot TUs + the test, never a widely-included header. `save_trait.hpp` stays include-light.
- **Mangled `typeid` names are not disk-stable** — W-3 must take the stable stringized name from the P1 X-list, never `typeid(C).name()`.
- **`kExplicit=false` is the trap, not a default policy.** The primary template is deliberately "no decision" (not "opt-out") so omission is a build error. Reviewers must reject making the primary default to `kSave=false`.
- **`C_VoxelSetNew` opt-in is provisional** — records intent; its serializer is genuinely hard (pool re-allocation + EntityId round-trip + W-10 interplay); flipping is one line.
- **Singletons need no special trait** — singleton-ness is W-6's concern; classify purely on data vs derived (SimClock opt-in; SpatialIndex/ActiveLodLevel opt-out).

### Sibling + in-flight reconciliation

- **First in the 7-child chain; nothing blocks P1.** #663's primitives are landed and verified; P1 doesn't consume them yet (P2 does).
- **No open PR touches `engine/world/`, `engine/prefabs/**/components/`, or `engine/asset/`** (open PRs are render sun-shadow + tooling docs). No component header is in flight, so the inventory won't go stale between filing and implementation.
- **Downstream contract for P2:** P2 consumes `shouldSave<C>()` / `saveVersion<C>()` and `AllEngineComponents`, adding the `ComponentId → serialize descriptor` runtime bridge P1 intentionally omits. Keep `save_trait.hpp` free of `EntityManager`/`ComponentId` includes so P2 layers without cycles — the trait is a pure compile-time fact table.
- **New-component contract to advertise:** any component added after P1 must gain an inventory line + `AllEngineComponents` entry (enforced by count backstop + compile gate). Call out in the PR description + CLAUDE.md note so the epic steward holds later children to it.
