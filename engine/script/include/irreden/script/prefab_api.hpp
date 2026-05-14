#ifndef IR_SCRIPT_PREFAB_API_H
#define IR_SCRIPT_PREFAB_API_H

// Lua prefab API: Prefab.register/spawn — v1 schema and behavioral contract in engine/script/CLAUDE.md "Prefab format".

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

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

// v1 schema version; unknown versions surface a diagnostic rather than silently misinterpreting fields.
constexpr int kPrefabSchemaVersion = 1;

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

// Load the prefab file registered under id, validate v1 schema, and instantiate at position.
SpawnResult spawnPrefab(IRScript::LuaScript &script, std::string_view id, IRMath::vec3 position);

} // namespace IRPrefab::Prefab

#endif /* IR_SCRIPT_PREFAB_API_H */
