#ifndef IR_SCRIPT_PREFAB_API_H
#define IR_SCRIPT_PREFAB_API_H

// Lua prefab API: Prefab.register/spawn — schema and behavioral contract in engine/script/CLAUDE.md
// "Prefab format".

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/lod_level.hpp>
#include <irreden/update/components/component_prefab_parts.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct PrefabShapeDescription {
    IRMath::SDF::ShapeType type_ = IRMath::SDF::ShapeType::BOX;
    IRMath::vec4 params_ = IRMath::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRMath::Color color_ = IRMath::Color{255, 255, 255, 255};
    std::uint32_t flags_ = IRMath::SDF::SHAPE_FLAG_VISIBLE;
};

// One `components = { <name_> = <fields_> }` entry. `fields_` is the Lua
// table-constructor source of the field overrides, e.g. `{ weight = 7 }`.
struct PrefabComponentDescription {
    std::string name_;
    std::string fields_;
};

struct PrefabPartDescription {
    std::string id_;
    std::string voxelRef_;
    std::optional<PrefabShapeDescription> shape_;
    IRComponents::C_LocalTransform transform_;
    IRComponents::RotationMode rotationMode_ = IRComponents::RotationMode::GRID;
    IRMath::ivec2 canvasSize_{0};
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    bool resident_ = false;
    std::vector<PrefabComponentDescription> components_;
};

struct PrefabDescription {
    int version_ = kPrefabSchemaVersion;
    std::vector<PrefabComponentDescription> components_;
    std::vector<PrefabPartDescription> parts_;
};

struct ManifestResult {
    std::optional<PrefabDescription> description_;
    std::string error_;

    bool ok() const {
        return description_.has_value();
    }
};

// Re-registering an id overwrites the prior path; clearPrefabs() resets between tests.
void registerPrefab(std::string id, std::string path);

// Returns nullopt if the id was never registered.
std::optional<std::string> prefabPath(std::string_view id);

// Test helper; not exposed to Lua.
void clearPrefabs();

// Read and write the declarative portion of a prefab manifest. The writer is
// the format owner used by authoring tools; emitted voxel_ref paths retain the
// caller's spelling and therefore follow the same cwd-relative rule as spawn.
// Reading resolves every `components` entry against the factory registry, so
// a manifest naming a Lua-registered component reads only in a process that
// registered it. The reader serializes each entry's table with sorted keys;
// a field holding a function or other non-data value is an error.
ManifestResult readManifest(IRScript::LuaScript &script, const std::string &path);
std::optional<std::string>
writeManifest(const std::string &path, const PrefabDescription &description);

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
