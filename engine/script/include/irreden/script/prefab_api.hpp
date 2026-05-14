#ifndef IR_SCRIPT_PREFAB_API_H
#define IR_SCRIPT_PREFAB_API_H

/// Lua prefab format — an entity template that references a `.vxs` voxel
/// set, an optional `.rig` rig, and a setup callback. Phase 5 of the
/// editor epic (#608); see `docs/design/entity-editor-epic.md` for the
/// long-form design and `engine/script/CLAUDE.md` "Prefab format" for
/// the runtime contract.
///
/// Lua schema (v1):
///
///     return {
///         prefab_version = 1,                  -- REQUIRED, must be 1
///         voxel_ref      = "foo.vxs",          -- OPTIONAL, loaded via loadVoxelSet
///         rig_ref        = "foo.rig",          -- OPTIONAL, loaded via loadRig
///         setup          = function(entity)    -- OPTIONAL, user-provided
///             -- IREntity.setComponent(entity, ...)
///         end,
///     }
///
/// Round-trip surface used by the editor (#608) is the same shape:
/// the component palette emits a Lua file matching this schema and
/// `Prefab.spawn(id, position)` reconstitutes the entity.
///
/// **v1 deferred** (filed as follow-ups):
/// - declarative `components = { C_Name = { ... } }` table — needs the
///   reflection layer the editor-epic doc flags as a Phase 5 risk
///   ("Component-palette schema"). Use the `setup` callback in the
///   meantime.
/// - `bind_point_overrides = { name = { offset = {...} } }` — needs a
///   runtime `C_BindPoints` component (only the `.rig` asset side ships
///   so far; see T-171 and engine/asset/include/irreden/asset/rig_format.hpp).
/// - `entity:bindPoint("name")` Lua API — same dependency as above.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace IRScript {
class LuaScript;
namespace detail {
/// Wire `Prefab.register` and `Prefab.spawn` into `script`'s Lua state.
/// Idempotent: re-binding overwrites the previous closures and clears
/// the in-process registry, so tests can call it once per fixture.
/// Called from `LuaScript::bindLuaDrivenEcs()`.
void bindPrefabApi(LuaScript &script);
} // namespace detail
} // namespace IRScript

namespace IRPrefab::Prefab {

/// v1 of the prefab Lua schema. Older builds reading a v1 prefab load
/// fine; newer builds reading a future v2 surface a clear diagnostic
/// rather than misinterpreting the fields.
constexpr int kPrefabSchemaVersion = 1;

/// Outcome of `spawnPrefab`. `entity_ == IREntity::kNullEntity` means
/// the spawn failed for one of the reasons listed in `error_` —
/// callers should check `entity_` before using it. The id and resolved
/// path are surfaced for diagnostics regardless of success.
struct SpawnResult {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    std::string id_;
    std::string path_;
    /// One-line diagnostic when `entity_ == IREntity::kNullEntity`.
    /// Empty on success. Same string is logged at error level by
    /// `spawnPrefab` itself; the field exists so call sites (Lua
    /// bindings, tests) can surface it without re-running the logger.
    std::string error_;
};

/// Add @p id → @p path to the in-process prefab registry. Re-registering
/// an existing id overwrites the prior path with a log; clearing happens
/// at process shutdown (no explicit lifetime owner today). Use
/// `clearPrefabs()` to reset between tests.
void registerPrefab(std::string id, std::string path);

/// Look up the registered path for @p id. Returns `std::nullopt` if
/// the id was never registered.
std::optional<std::string> prefabPath(std::string_view id);

/// Erase every entry from the in-process registry. Test helper; not
/// exposed to Lua.
void clearPrefabs();

/// Load the prefab Lua file registered under @p id and instantiate it
/// at @p position. The Lua file is executed via @p script (whose
/// `sol::state` provides the eval environment); the file must return a
/// table matching the v1 schema documented at the top of this header.
///
/// Behavior:
/// - Validates `prefab_version == kPrefabSchemaVersion`. Missing or
///   mismatched versions return `SpawnResult{entity_ = kNullEntity}`
///   with a `VersionMismatch`-style error string.
/// - Creates an entity with `C_Position3D(position)`.
/// - If `voxel_ref` is present, loads the `.vxs` via
///   `IRAsset::loadVoxelSet`. The loaded shape records / dense voxel
///   data are validated to load cleanly but **not** attached as
///   runtime components in v1 (`C_VoxelSetNew` requires an active
///   render canvas pool). A follow-up task wires the attachment.
/// - If `rig_ref` is present, loads the `.rig` via `IRAsset::loadRig`
///   and attaches `C_JointHierarchy` via
///   `IRPrefab::Rig::toComponent`. Bind-points are loaded but not
///   attached pending the runtime `C_BindPoints` component.
/// - If `setup` is a Lua function, invokes it with the entity wrapped
///   as `LuaEntity{entity}`. Lua errors in `setup` propagate as a
///   failed `SpawnResult` with the error string forwarded.
///
/// Additive component packs: unknown top-level keys in the prefab
/// table are silently ignored, so a future schema field doesn't break
/// older loaders. New required fields bump `kPrefabSchemaVersion`.
SpawnResult spawnPrefab(IRScript::LuaScript &script, std::string_view id, IRMath::vec3 position);

} // namespace IRPrefab::Prefab

#endif /* IR_SCRIPT_PREFAB_API_H */
