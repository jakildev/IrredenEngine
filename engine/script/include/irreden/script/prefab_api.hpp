#ifndef IR_SCRIPT_PREFAB_API_H
#define IR_SCRIPT_PREFAB_API_H

// Lua prefab API: Prefab.register/spawn — schema and behavioral contract in engine/script/CLAUDE.md
// "Prefab format".

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/lod_level.hpp>
#include <irreden/update/components/component_prefab_parts.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace IRScript {
class LuaScript;
namespace detail {
// Wire Prefab.register and Prefab.spawn into the Lua state; called from bindLuaDrivenEcs().
void bindPrefabApi(LuaScript &script);
} // namespace detail
} // namespace IRScript

namespace IRPrefab::Prefab {

// Newest schema version: v2 adds the `parts` list. Every version from
// kPrefabSchemaVersionMin up loads; others surface a diagnostic rather than
// silently misinterpreting fields.
constexpr int kPrefabSchemaVersion = 2;
constexpr int kPrefabSchemaVersionMin = 1;

// Outcome of spawnPrefab; check entity_ != kNullEntity before use.
struct SpawnResult {
    IREntity::EntityId entity_ = IREntity::kNullEntity;
    std::string id_;
    std::string path_;
    // Non-empty iff entity_ == kNullEntity; same string spawnPrefab logs at error level.
    std::string error_;
};

// Re-registering an id overwrites the prior path; clearPrefabs() resets between tests.
void registerPrefab(std::string id, std::string path);

// Returns nullopt if the id was never registered.
std::optional<std::string> prefabPath(std::string_view id);

// Test helper; not exposed to Lua.
void clearPrefabs();

// Load the prefab file registered under id, validate its schema, and instantiate at position.
// A v2 root spawns the parts whose band holds its resolved tier as CHILD_OF children.
SpawnResult spawnPrefab(IRScript::LuaScript &script, std::string_view id, IRMath::vec3 position);

// Creates part `index` of `root`: the id is reserved now and written to the
// slot, the part is built at the next flushStructuralChanges. Safe inside a
// tick. A root destroyed or marked for deletion by then gets no part. The part
// loads its voxel_ref on its first spawn.
void stagePartSpawn(IREntity::EntityId root, IRComponents::C_PrefabParts &parts, std::size_t index);

// Marks the slot's part tree and its detached canvas for deletion and clears
// the slot. Safe inside a tick.
void despawnPart(IRComponents::PrefabPartSlot &slot);

// Pins a live resident part's content to `tier`; its content draws only while
// `tier` is inside the part's band.
void pinResidentPart(const IRComponents::PrefabPartSlot &slot, IRRender::LodLevel tier);

} // namespace IRPrefab::Prefab

#endif /* IR_SCRIPT_PREFAB_API_H */
