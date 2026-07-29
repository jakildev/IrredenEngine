## Plan: persist: grow the process-default SaveRegistry toward full engine-component coverage (P7 follow-up)

- **Issue:** #2242
- **Model:** opus — the mechanism is committed below; the remaining work is ~20 mostly-mechanical serializers plus test wiring, bounded but multi-module
- **Date:** 2026-07-20

### Scope

Retire the hand-curated 4-component list in `makeDefaultSaveRegistry()` (`engine/world/src/world_default_registry.cpp`) and derive the process-default registry's membership from the inventory + a compile-time serializability filter, so every opted-in component is registered automatically and a future opt-in without a serializer is a **build error**, never a silent save omission. Ship the `SaveSerialize<C>` specializations that unlock this for the ~20 opted-in heap-owning components.

### Verified current state (source-verified 2026-07-20 against master 5c53d2e5)

- `world_default_registry.cpp` registers exactly 4 components (`C_VoxelSetNew`, `C_LocalTransform`, `C_PositionInt3D`, `C_SizeInt3D`).
- Exactly **one** production `SaveSerialize` specialization exists: `C_VoxelSetNew` (`engine/prefabs/irreden/voxel/voxel_set_serialize.hpp`); the two others in the tree are test-local (`test/world/{world_snapshot,component_migration}_test.cpp`).
- The inventory (`save_component_inventory.hpp`) has 165 components, ~110 opted IN. Struct-level audit of every opted-in component whose header contains heap types confirms the issue's premise — these opted-in components are **not** trivially copyable and would trip the primary template's `static_assert` if registered:
  - **string-bearing:** `C_Name`, `C_TextSegment`, `C_JointName`, `C_Timer`, `C_Stopwatch`, `C_Cycle`, `C_SpriteAnimation`, `C_Example`
  - **vector-bearing:** `C_Skeleton` (`vector<EntityId>` + `vector<SQT>`), `C_MidiSequence` (`vector<pair<int, C_MidiMessage>>`), `C_PeriodicIdle` (`vector<PeriodStage>`), `C_TrianglesOnlySet` (`vector<Color>` + `vector<Distance>`), `C_TriangleCanvasBackground` (private `m_colors` / `m_randomColorData` / `m_patternMask`)
  - **map-bearing:** `C_BindPoints` (`unordered_map<string, BindPointRuntime>`)
  - **widget family** (`component_widget.hpp`): `C_WidgetPanel/Label/Button/Slider/Checkbox/List/Dropdown/Radio/TextInput` hold `string` / `vector<string>`. `C_Widget`, `C_WidgetState`, `C_WidgetColorSwatch`, `C_WidgetScroll`, `C_Splitter` appear POD — phase 0 confirms which need nothing.
- **No opted-in component holds `std::function` / `sol::` / raw-owning-pointer state** (grep across every opted-in header). The known non-serializable shapes (`C_LerpEntity`, `C_LambdaModifiers`) are already opted OUT — so no opt-in→opt-out flips are *expected*; the flip remains the documented bail (below) if phase 0 surfaces a hidden case.
- `BinaryWriter/Reader` already provide `writeString`/`readString` (+ `writeVarUInt`, sized ints/floats); there is no vector helper yet.
- C++ standard is **23** (root `CMakeLists.txt`), so concepts/constrained partial specializations are available.
- No open PR touches the persist surface; no open sibling issue overlaps (checked open PR list + open issues, 2026-07-20).
- Not compiler-verified on this host (macOS builds are broken, #2449) — hence phase 0 below runs the exact enumeration on the implementer's Linux host.

### Approach

**Committed mechanism — `SaveSerializable<C>` concept via a split primary, NOT a hand-maintained companion trait.** The issue sketches a `HasExplicitSaveSerialize<C>` trait; a companion trait has a dual-bookkeeping failure mode (serializer written, trait line forgotten → component silently drops out of the default registry — the exact silent-omission class this issue exists to kill). Instead make serializability self-detecting:

```cpp
// save_serialize.hpp
template <typename C> struct SaveSerialize;          // primary: declared, undefined

template <typename C>
    requires std::is_trivially_copyable_v<C>
struct SaveSerialize<C> { /* current raw-byte-image body, static_assert dropped */ };

// A component is serializable iff SaveSerialize<C> is usable: the constrained
// TC fast path, or an explicit specialization (C_VoxelSetNew et al).
template <typename C>
concept SaveSerializable = requires(IRAsset::BinaryWriter &w, const C &v, IRAsset::BinaryReader &r) {
    SaveSerialize<C>::write(w, v);
    { SaveSerialize<C>::read(r) } -> std::same_as<IRAsset::Result<C>>;
};
```

Existing full specializations (`C_VoxelSetNew`, the two test-local ones) are untouched — full specializations of an undefined primary are fine, and TC types now match the constrained partial specialization with the identical body. The friendly diagnostic moves to registration: `static_assert(SaveSerializable<C>, "<current message>")` at the top of `SaveRegistry::registerComponent`'s `shouldSave<C>()` branch.

**Phase 0 (probe, first commit on the Linux host).** Two cheap premise checks:
1. A scratch TU proving the constrained-partial-specialization + concept detection compiles on gcc-13 (mechanism premise; expected: compiles, concept is `false` for an undefined-primary type, `true` for TC and explicitly-specialized types).
2. Convert `makeDefaultSaveRegistry()` to the tuple walk (below) **before writing any serializer** and build once: the compile-error list is the exact, compiler-authoritative serializer work list. Expected reading ≈ the ~20 components enumerated above. **Bail path:** any surfaced component whose members can't honestly round-trip (hidden callback/handle state) is flipped `IR_SAVE_OPT_OUT` with a one-line class comment in the inventory — do not write a lossy serializer that silently defaults fields; if a flip feels wrong (the component is clearly authored gameplay state), stop and comment the finding on this issue for re-plan rather than improvising.

**Phase 1 — mechanism.**
- `save_serialize.hpp`: the split above + concept.
- `save_registry.hpp`: the friendly `static_assert`; no other behavior change.
- `world_default_registry.cpp`: replace the curated list with a tuple walk:
  ```cpp
  []<typename... Cs>(std::type_identity<std::tuple<Cs...>>) {
      (registry.registerComponent<Cs>(), ...);
  }(std::type_identity<AllEngineComponents>{});
  ```
  `registerComponent`'s existing `if constexpr (shouldSave<C>())` gate skips opt-outs; every opted-in element instantiates its serializer, so **this TU is the completeness gate** — a future `IR_SAVE_OPT_IN` without a serializer (or tuple entry, already asserted) breaks this build with the friendly message. Keep `voxel_set_serialize.hpp`'s include; add the new serializer headers (phase 2). Do **not** add serializer includes to `save_component_inventory.hpp` — it stays a pure decision table (and stays out of the prefab-serializer dependency business).
- New `engine/world/include/irreden/world/save_serialize_common.hpp`: header-only helpers `writeTrivialVector<T>` / `readTrivialVector<T>` (varuint count + raw records, the `voxel_set_serialize.hpp` record pattern, with a per-use `static_assert(std::is_trivially_copyable_v<T>)`) so the vector serializers don't hand-roll the loop five times. String round-trip uses the existing `writeString`/`readString`.

**Phase 2 — serializers, grouped per prefab module** (one header per module, mirroring the `voxel_set_serialize.hpp` placement precedent; ~20 specializations total, each `IR_SAVE_OPT_IN` version stays 1 since nothing has ever been written at these schemas):
- `engine/prefabs/irreden/common/save_serializers_common.hpp` — `C_Name`, `C_Timer`, `C_Stopwatch`, `C_Cycle`, `C_Example` (+ any phase-0 stragglers from common).
- `engine/prefabs/irreden/render/save_serializers_render.hpp` — `C_TextSegment`, the string/vector widget structs, `C_TrianglesOnlySet`, `C_TriangleCanvasBackground`, `C_SpriteAnimation`.
- `engine/prefabs/irreden/voxel/save_serializers_voxel.hpp` — `C_Skeleton`, `C_JointName`, `C_BindPoints`.
- `engine/prefabs/irreden/audio/save_serializers_audio.hpp` — `C_MidiSequence`.
- `engine/prefabs/irreden/update/save_serializers_update.hpp` — `C_PeriodicIdle`.

Key decisions inside phase 2:
- `C_BindPoints`: write map entries **sorted by key** — unordered iteration order would break the double-save byte-identity contract (world-snapshot criterion 6).
- `C_MidiSequence` / `C_PeriodicIdle` element types (`C_MidiMessage`, `PeriodStage`) and `C_Skeleton`'s `SQT`: `static_assert` trivially-copyable at the use site, then raw records via the vector helper.
- `C_Skeleton.joints_` (`EntityId`s) round-trips as plain values — the snapshot's exact-id restore contract already blesses this (the inventory's own Class E comment).
- `C_TriangleCanvasBackground`: serialize via its public accessor/ctor surface; where the private `m_*` vectors aren't reachable, add a `friend struct IRWorld::SaveSerialize<C_TriangleCanvasBackground>;` declaration rather than widening the public API.
- Widget structs that phase 0 proves TC get **no** serializer (primary fast path).

**Phase 3 — tests + docs.**
- New `test/world/save_serializers_test.cpp`: value round-trip per serializer family incl. empty string, empty vector, and a `C_BindPoints` double-save byte-identity case (map-order determinism).
- Extend `test/world/persist_round_trip_test.cpp`: a world carrying `C_Name` + `C_Skeleton` + one widget component round-trips with values asserted, and double-save stays byte-identical.
- Extend `test/script/lua_world_snapshot_test.cpp`: `C_Name` round-trip **through the actual `IRPersist` Lua surface** — the mandatory wiring-level test per `world/CLAUDE.md` "Process-default registry" (#2244 rule); this is the issue's named acceptance component.
- A registry-membership positive-fire check (in `save_serializers_test.cpp` or the snapshot test): `makeDefaultSaveRegistry().size()` equals the inventory's opted-in count computed from `AllEngineComponents` at compile time (a `detail::countOptIns<Tuple>()` sibling of `allExplicit`) — proves membership is *derived*, not curated, and fails if the two ever diverge.
- Docs: update `engine/world/CLAUDE.md` "Process-default registry" (curated-subset story → derived-membership story + the new-opt-in-needs-serializer build gate), the header comments in `world_default_registry.cpp` / `save_registry.hpp` scope notes / `save_serialize.hpp`.

One task, one PR — the pieces are coupled (the tuple walk cannot land without the serializers, and the serializers are unverifiable in the default registry without the walk); phase 0's error-list step keeps the serializer batch honest and bounded.

### Affected files

- `engine/world/include/irreden/world/save_serialize.hpp` — primary split + `SaveSerializable` concept
- `engine/world/include/irreden/world/save_serialize_common.hpp` — **new**, vector round-trip helpers
- `engine/world/include/irreden/world/save_registry.hpp` — friendly `static_assert` in `registerComponent`
- `engine/world/include/irreden/world/save_trait.hpp` — `detail::countOptIns<Tuple>()` (sibling of `allExplicit`)
- `engine/world/src/world_default_registry.cpp` — curated list → tuple walk + serializer includes (curated list retired)
- `engine/prefabs/irreden/{common,render,voxel,audio,update}/save_serializers_<module>.hpp` — **new**, ~20 specializations
- `engine/prefabs/irreden/render/components/component_triangle_canvas_background.hpp` — friend declaration (only if the accessor surface is insufficient)
- `engine/world/include/irreden/world/save_component_inventory.hpp` — only if phase 0 forces an opt-out flip (none expected)
- `test/world/save_serializers_test.cpp` — **new**
- `test/world/persist_round_trip_test.cpp`, `test/script/lua_world_snapshot_test.cpp` — extended
- `engine/world/CLAUDE.md` — registry section rewrite

### Acceptance criteria

1. `makeDefaultSaveRegistry()` membership is derived: the registry-size == compile-time opted-in count assertion passes (positive-fire — the count moves from 4 to ~110), and `world_default_registry.cpp` contains no per-component register lines.
2. Lua `IRPersist` round-trip of a world using `C_Name` preserves the name value, asserted through the real binding path in `test/script/lua_world_snapshot_test.cpp`.
3. New serializer round-trips pass, including empty-string/empty-vector edges and the `C_BindPoints` sorted-key double-save byte-identity case.
4. `persist_round_trip_test` extended world double-save stays byte-identical.
5. `linux-debug` build + full `test/world` + `test/script` suites green. macOS: gated on #2449 (fresh builds broken there); route through the normal cross-host smoke label rather than blocking the PR on a macOS build.
6. A deliberately serializer-less `IR_SAVE_OPT_IN` (temporarily added in a scratch check, not committed) fails the `world_default_registry.cpp` build with the friendly message — the completeness gate demonstrably fires.

### Gotchas

- **Constraint placement matters:** constraining the *primary* template would make the `C_VoxelSetNew` full specialization ill-formed (explicit-specialization args must satisfy the primary's constraints). The committed shape — undefined primary + constrained *partial* specialization — avoids this; phase 0's scratch TU exists to prove it on the shipping compiler before anything else lands.
- `unordered_map` iteration order is non-deterministic per process — any map-bearing serializer must sort before writing (byte-identity contract).
- Old saves stay loadable by construction (CMPN is name-keyed; unknown names skip; all new schemas are v1) — no migration work in this task.
- Registry construction cost grows from 4 to ~110 entries per save/load call — documented as fine (`save_registry.hpp`: never per-frame), don't cache it (session-local `ComponentId`s must stay live, per the existing header note).
- Do not widen component public APIs for serialization — `friend struct SaveSerialize<...>` is the sanctioned reach into privates (single use expected: `C_TriangleCanvasBackground`).
- `C_GeometricShape` keeps its one-representative (`SPHERE`) registration; other instantiations remain unsaved — pre-existing inventory decision, out of scope here.
- The implementer must work on a Linux host (macOS engine builds are broken, #2449).

### Cross-system audit (SaveSerialize shape change)

All `SaveSerialize` consumers, by grep: `save_registry.hpp` (`registerComponent` — gains the assert, otherwise unchanged), `voxel_set_serialize.hpp` (full specialization — unaffected), `test/world/world_snapshot_test.cpp` + `test/world/component_migration_test.cpp` (test-local full specializations — unaffected; their TC test components move from primary to the constrained partial specialization with an identical body). `world_snapshot.cpp` and `chunk_persistence.*` reach serialization only through registry hooks — no direct dependence on the template shape.

