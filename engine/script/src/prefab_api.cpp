#include <irreden/script/prefab_api.hpp>

#include <irreden/asset/rig_format.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/voxel/rig_bridge.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace IRPrefab::Prefab {

struct PartSpec {
    std::string id_;
    // At most one of voxelRef_ / shape_; a part with neither is a bare node.
    std::string voxelRef_;
    std::optional<PrefabShapeDescription> shape_;
    IRComponents::C_LocalTransform transform_;
    IRComponents::RotationMode rotationMode_ = IRComponents::RotationMode::GRID;
    IRMath::ivec2 canvasSize_{0};
    IRRender::LodLevel lodMin_ = IRRender::LodLevel::LOD_4;
    IRRender::LodLevel lodMax_ = IRRender::LodLevel::LOD_0;
    bool resident_ = false;
    // Resolved at parse, so a part spawned on a later tier change cannot fail
    // on an unknown component name.
    std::vector<std::pair<const ComponentFactory *, sol::table>> components_;
};

// The sol::table references die with the last C_PrefabParts (or staged part
// spawn) holding the manifest; both are torn down before World's sol::state.
struct PartsManifest {
    std::string prefabId_;
    std::vector<PartSpec> parts_;
    // Indexed like parts_; filled on the part's first spawn.
    mutable std::vector<std::shared_ptr<const IRAsset::VoxelSetAllFile>> voxels_;
};

namespace {

/// In-process registry. Module-local; no cross-process or cross-World
/// sharing. Mirrors the lifetime story of `IRPrefab::Modifier`'s
/// `globalFieldRegistry()` — process-singleton, cleared by tests via
/// `clearPrefabs()`.
std::unordered_map<std::string, std::string> &registry() {
    static std::unordered_map<std::string, std::string> g_registry;
    return g_registry;
}

/// Part voxel files loaded by any live manifest, keyed by path, so every root
/// of one prefab shares one copy. Holds no ownership.
std::unordered_map<std::string, std::weak_ptr<const IRAsset::VoxelSetAllFile>> &partVoxelCache() {
    static std::unordered_map<std::string, std::weak_ptr<const IRAsset::VoxelSetAllFile>> g_cache;
    return g_cache;
}

/// Build an error-state result. Logs at error level so failures are
/// visible even when the caller discards the returned struct.
SpawnResult makeError(std::string id, std::string path, std::string message) {
    IRE_LOG_ERROR("Prefab.spawn('{}'): {} (path='{}')", id.c_str(), message.c_str(), path.c_str());
    SpawnResult r;
    r.id_ = std::move(id);
    r.path_ = std::move(path);
    r.error_ = std::move(message);
    return r;
}

/// Reads `rotation_mode` and, for a canvas-owning mode, `canvas_size` from
/// `table` (the prefab root or one part). Returns an error message on a bad
/// value. The schema takes the `IRComponent.RotationMode.*` integer, never a
/// string name, so the Lua surface stays in lockstep with the C++ enum.
std::optional<std::string> parseRotationMode(
    const sol::table &table, IRComponents::RotationMode &mode, IRMath::ivec2 &canvasSize
) {
    sol::object modeObj = table["rotation_mode"];
    if (modeObj.valid() && modeObj.get_type() != sol::type::lua_nil) {
        if (modeObj.get_type() == sol::type::string) {
            return std::string{"rotation_mode must be an "
                               "IRComponent.RotationMode.{GRID,DETACHED,DETACHED_REVOXELIZE} "
                               "value; string names are not accepted"};
        }
        if (!modeObj.is<lua_Integer>()) {
            return std::string{
                "rotation_mode must be an "
                "IRComponent.RotationMode.{GRID,DETACHED,DETACHED_REVOXELIZE} value"
            };
        }
        const lua_Integer raw = modeObj.as<lua_Integer>();
        if (raw < static_cast<lua_Integer>(IRComponents::RotationMode::kFirst) ||
            raw > static_cast<lua_Integer>(IRComponents::RotationMode::kLast)) {
            return "rotation_mode=" + std::to_string(raw) +
                   " not recognized (expected IRComponent.RotationMode.GRID, .DETACHED, or "
                   ".DETACHED_REVOXELIZE)";
        }
        mode = static_cast<IRComponents::RotationMode>(raw);
    }

    if (!IRPrefab::RotationMode::ownsEntityCanvas(mode)) {
        return std::nullopt;
    }
    sol::optional<sol::table> sizeOpt = table["canvas_size"];
    if (!sizeOpt) {
        return std::string{
            "rotation_mode=IRComponent.RotationMode.DETACHED (or DETACHED_REVOXELIZE) requires "
            "canvas_size = { x, y }"
        };
    }
    sol::optional<int> wOpt = (*sizeOpt)["x"];
    sol::optional<int> hOpt = (*sizeOpt)["y"];
    if (!wOpt || !hOpt) {
        return std::string{"canvas_size must be a table with integer x and y fields"};
    }
    if (*wOpt <= 0 || *hOpt <= 0) {
        return std::string{"canvas_size.x and canvas_size.y must be positive"};
    }
    canvasSize = IRMath::ivec2{*wOpt, *hOpt};
    return std::nullopt;
}

/// Resolves a `components = { C_Foo = { ... } }` block to its factories
/// without applying them.
std::optional<std::string> parseComponents(
    const sol::table &table,
    std::vector<std::pair<const ComponentFactory *, sol::table>> &components
) {
    sol::optional<sol::table> componentsOpt = table["components"];
    if (!componentsOpt) {
        return std::nullopt;
    }
    for (auto &kv : *componentsOpt) {
        sol::optional<std::string> nameOpt = kv.first.as<sol::optional<std::string>>();
        if (!nameOpt) {
            return std::string{"components keys must be component-name strings"};
        }
        if (kv.second.get_type() != sol::type::table) {
            return std::string{"components['"} + *nameOpt + "'] must be a table of field overrides";
        }
        const ComponentFactory *factory = findComponentFactory(*nameOpt);
        if (!factory) {
            return std::string{"no factory registered for component '"} + *nameOpt +
                   "' (the binding's *_lua.hpp must call "
                   "IRScript::registerComponentFactoryFor and the creation must include it)";
        }
        components.emplace_back(factory, kv.second.as<sol::table>());
    }
    return std::nullopt;
}

std::optional<std::string>
parseLodTier(const sol::table &lod, const char *key, IRRender::LodLevel &tier) {
    sol::object tierObj = lod[key];
    if (!tierObj.valid() || tierObj.get_type() == sol::type::lua_nil) {
        return std::nullopt;
    }
    if (!tierObj.is<lua_Integer>() || tierObj.get_type() == sol::type::string) {
        return std::string{"lod."} + key + " must be an IRRender.LodLevel value";
    }
    const lua_Integer raw = tierObj.as<lua_Integer>();
    if (raw < static_cast<lua_Integer>(IRRender::LodLevel::LOD_0) ||
        raw > static_cast<lua_Integer>(IRRender::LodLevel::LOD_4)) {
        return std::string{"lod."} + key + "=" + std::to_string(raw) +
               " is outside IRRender.LodLevel.LOD_0 .. LOD_4";
    }
    tier = static_cast<IRRender::LodLevel>(raw);
    return std::nullopt;
}

std::optional<std::string>
parsePartShape(const sol::table &shapeTable, PrefabShapeDescription &shape) {
    sol::object typeObj = shapeTable["type"];
    if (!typeObj.is<lua_Integer>() || typeObj.get_type() == sol::type::string) {
        return std::string{"shape.type must be an IRShape value"};
    }
    try {
        shape.type_ = IRScript::detail::shapeTypeFromLua(typeObj.as<lua_Integer>(), "shape.type");
    } catch (const sol::error &e) {
        return std::string{e.what()};
    }
    sol::object paramsObj = shapeTable["params"];
    if (paramsObj.valid() && paramsObj.get_type() != sol::type::lua_nil) {
        shape.params_ = IRScript::vec4FromLua(paramsObj);
    }
    sol::object colorObj = shapeTable["color"];
    if (colorObj.valid() && colorObj.get_type() != sol::type::lua_nil) {
        shape.color_ = IRScript::colorFromLua(colorObj);
    }
    if (sol::optional<std::uint32_t> flags = shapeTable["flags"]; flags) {
        shape.flags_ = *flags;
    }
    return std::nullopt;
}

std::optional<std::string> parsePart(const sol::table &partTable, PartSpec &part) {
    sol::optional<std::string> idOpt = partTable["id"];
    if (!idOpt || idOpt->empty()) {
        return std::string{"needs a non-empty string id"};
    }
    part.id_ = *idOpt;

    sol::optional<std::string> voxelRef = partTable["voxel_ref"];
    sol::object shapeObj = partTable["shape"];
    const bool hasShape = shapeObj.valid() && shapeObj.get_type() != sol::type::lua_nil;
    if (voxelRef && hasShape) {
        return std::string{"takes voxel_ref or shape, not both"};
    }
    if (voxelRef) {
        // The load itself waits for the part's first spawn; a missing file
        // fails the root spawn instead of a later tier change.
        if (!std::filesystem::exists(*voxelRef)) {
            return "voxel_ref not found: " + *voxelRef;
        }
        part.voxelRef_ = *voxelRef;
    }
    if (hasShape) {
        if (shapeObj.get_type() != sol::type::table) {
            return std::string{"shape must be a table"};
        }
        PrefabShapeDescription shape;
        if (auto error = parsePartShape(shapeObj.as<sol::table>(), shape)) {
            return error;
        }
        part.shape_ = shape;
    }

    sol::object transformObj = partTable["transform"];
    if (transformObj.valid() && transformObj.get_type() != sol::type::lua_nil) {
        if (transformObj.get_type() != sol::type::table) {
            return std::string{"transform must be a table"};
        }
        sol::table transform = transformObj.as<sol::table>();
        part.transform_.translation_ = IRScript::vec3FromLua(transform["translation"]);
        part.transform_.rotation_ = IRScript::quatFromLua(transform["rotation"]);
        sol::object scaleObj = transform["scale"];
        if (scaleObj.valid() && scaleObj.get_type() != sol::type::lua_nil) {
            part.transform_.scale_ = IRScript::vec3FromLua(scaleObj);
        }
    }

    if (auto error = parseRotationMode(partTable, part.rotationMode_, part.canvasSize_)) {
        return error;
    }

    sol::object lodObj = partTable["lod"];
    if (lodObj.valid() && lodObj.get_type() != sol::type::lua_nil) {
        if (lodObj.get_type() != sol::type::table) {
            return std::string{"lod must be a table { fine = ..., coarse = ... }"};
        }
        sol::table lod = lodObj.as<sol::table>();
        if (auto error = parseLodTier(lod, "fine", part.lodMax_)) {
            return error;
        }
        if (auto error = parseLodTier(lod, "coarse", part.lodMin_)) {
            return error;
        }
        if (part.lodMax_ > part.lodMin_) {
            return std::string{"lod.fine must not be coarser than lod.coarse"};
        }
    }

    if (sol::optional<bool> resident = partTable["resident"]; resident) {
        part.resident_ = *resident;
    }

    return parseComponents(partTable, part.components_);
}

std::optional<std::string> parseParts(
    const sol::table &prefab, const std::string &prefabId, std::shared_ptr<PartsManifest> &manifest
) {
    sol::object partsObj = prefab["parts"];
    if (!partsObj.valid() || partsObj.get_type() == sol::type::lua_nil) {
        return std::nullopt;
    }
    if (partsObj.get_type() != sol::type::table) {
        return std::string{"parts must be an array of part tables"};
    }
    sol::table partsTable = partsObj.as<sol::table>();
    auto parsed = std::make_shared<PartsManifest>();
    parsed->prefabId_ = prefabId;
    std::unordered_set<std::string> ids;
    const std::size_t count = partsTable.size();
    parsed->parts_.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        const std::string where = "parts[" + std::to_string(i) + "]";
        sol::object partObj = partsTable[i];
        if (partObj.get_type() != sol::type::table) {
            return where + " must be a table";
        }
        PartSpec part;
        if (auto error = parsePart(partObj.as<sol::table>(), part)) {
            return where + ": " + *error;
        }
        if (!ids.insert(part.id_).second) {
            return where + ": duplicate part id '" + part.id_ + "'";
        }
        parsed->parts_.push_back(std::move(part));
    }
    parsed->voxels_.resize(parsed->parts_.size());
    manifest = std::move(parsed);
    return std::nullopt;
}

std::shared_ptr<const IRAsset::VoxelSetAllFile> loadPartVoxels(const std::string &path) {
    auto &cache = partVoxelCache();
    if (auto it = cache.find(path); it != cache.end()) {
        if (auto shared = it->second.lock()) {
            return shared;
        }
    }
    auto loaded = IRAsset::loadVoxelSet(path);
    if (!loaded.ok()) {
        return nullptr;
    }
    auto shared = std::make_shared<const IRAsset::VoxelSetAllFile>(std::move(loaded.value_));
    cache[path] = shared;
    return shared;
}

/// The world transform PROPAGATE_TRANSFORM will give a child, so a part or
/// shape child created after this tick's propagation draws in place on its
/// first frame instead of at the world origin.
IRComponents::C_WorldTransform composeWorld(
    const IRComponents::C_WorldTransform &parent, const IRComponents::C_LocalTransform &local
) {
    const IRMath::SQT world = IRMath::sqtCompose(
        IRMath::SQT{parent.scale_, parent.rotation_, parent.translation_},
        IRMath::SQT{local.scale_, local.rotation_, local.translation_}
    );
    return IRComponents::C_WorldTransform{world.translation_, world.rotation_, world.scale_};
}

/// Attaches a loaded `.vxs` to `entity`: one `CHILD_OF` `C_ShapeDescriptor`
/// child per SHAPES record, whose `offset_` composes through the child's
/// `C_LocalTransform`; DENSE data as `C_VoxelSetNew` on `entity` itself. The
/// dense adapter is headless-safe: with no active canvas it stages records for
/// a later canvas-attach pass. Per-record rotation, CSG op and bone id are not
/// consumed by the renderer and are not stamped.
void attachVoxelContent(
    IREntity::EntityId entity,
    const IRAsset::VoxelSetAllFile &voxels,
    std::vector<IREntity::EntityId> &shapeChildren
) {
    if (voxels.mode_ == IRAsset::VoxelSetMode::SHAPES ||
        voxels.mode_ == IRAsset::VoxelSetMode::HYBRID) {
        const IRComponents::C_WorldTransform parentWorld =
            IREntity::getComponent<IRComponents::C_WorldTransform>(entity);
        shapeChildren.reserve(shapeChildren.size() + voxels.shapeRecords_.size());
        for (const auto &record : voxels.shapeRecords_) {
            IRComponents::C_ShapeDescriptor descriptor{
                static_cast<IRMath::SDF::ShapeType>(record.shapeTypeId_),
                record.params_,
                record.color_
            };
            descriptor.flags_ = record.flags_;
            const IRComponents::C_LocalTransform local{record.offset_};
            const IREntity::EntityId child =
                IREntity::createEntity(local, composeWorld(parentWorld, local), descriptor);
            IREntity::setParent(child, entity);
            shapeChildren.push_back(child);
        }
    }

    if (voxels.mode_ == IRAsset::VoxelSetMode::DENSE ||
        voxels.mode_ == IRAsset::VoxelSetMode::HYBRID) {
        IRComponents::C_VoxelSetNew voxelSet = IRPrefab::DenseVoxel::toComponent(voxels.dense_);
        if (voxelSet.recordCount() > 0) {
            IREntity::setComponent(entity, std::move(voxelSet));
        }
    }
}

/// Allocates the per-entity canvas a canvas-owning rotation mode needs. In a
/// headless context (no RenderManager) the entity stays tagged with the mode
/// so a later `IRPrefab::RotationMode::setMode` call picks the canvas up.
void attachEntityCanvas(
    IREntity::EntityId entity,
    IRComponents::RotationMode mode,
    const std::string &canvasName,
    IRMath::ivec2 canvasSize,
    const std::string &prefabId
) {
    if (!IRPrefab::RotationMode::ownsEntityCanvas(mode)) {
        return;
    }
    if (IRRender::g_renderManager == nullptr) {
        IRE_LOG_WARN(
            "Prefab.spawn('{}'): rotation_mode=IRComponent.RotationMode.DETACHED "
            "(or DETACHED_REVOXELIZE) requested without an active RenderManager; "
            "skipping canvas allocation.",
            prefabId.c_str()
        );
        return;
    }
    IREntity::setComponent(entity, IRPrefab::EntityCanvas::create(canvasName, canvasSize));
}

IRRender::LodLevel resolveSpawnTier(IREntity::EntityId root) {
    const auto *active = IREntity::singletonOrNull<IRComponents::C_ActiveLodLevel>();
    const auto pin = IREntity::getComponentOptional<IRComponents::C_LodTierOverride>(root);
    return IRRender::resolveEntityLod(
        active != nullptr ? active->current_ : IRRender::LodLevel::LOD_4,
        pin ? *pin : nullptr
    );
}

std::string luaString(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

void writeVec3(std::ostream &out, IRMath::vec3 value) {
    out << "{ x = " << value.x << ", y = " << value.y << ", z = " << value.z << " }";
}

void writeVec4(std::ostream &out, IRMath::vec4 value) {
    out << "{ x = " << value.x << ", y = " << value.y << ", z = " << value.z << ", w = " << value.w
        << " }";
}

void writeColor(std::ostream &out, IRMath::Color value) {
    out << "{ r = " << static_cast<int>(value.red_) << ", g = " << static_cast<int>(value.green_)
        << ", b = " << static_cast<int>(value.blue_) << ", a = " << static_cast<int>(value.alpha_)
        << " }";
}

/// Builds manifest part `index` onto `part`, an entity with no components yet,
/// and parents it to `root`. A resident part's content takes the part's band
/// and a tier pin at `tier`, the root's settled tier.
void buildPart(
    IREntity::EntityId root,
    IREntity::EntityId part,
    const PartsManifest &manifest,
    std::size_t index,
    IRRender::LodLevel tier,
    IRComponents::PrefabPartSlot &slot
) {
    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_ENTITY_OPS);
    const PartSpec &spec = manifest.parts_[index];

    IREntity::setComponent(part, spec.transform_);
    IREntity::setComponent(
        part,
        composeWorld(IREntity::getComponent<IRComponents::C_WorldTransform>(root), spec.transform_)
    );
    IREntity::setComponent(part, IRComponents::C_RotationMode{spec.rotationMode_});
    IREntity::setParent(part, root);

    attachEntityCanvas(
        part,
        spec.rotationMode_,
        manifest.prefabId_ + "_" + spec.id_ + "_canvas",
        spec.canvasSize_,
        manifest.prefabId_
    );

    std::vector<IREntity::EntityId> shapeChildren;
    if (spec.shape_) {
        IRComponents::C_ShapeDescriptor descriptor{
            spec.shape_->type_,
            spec.shape_->params_,
            spec.shape_->color_
        };
        descriptor.flags_ = spec.shape_->flags_;
        IREntity::setComponent(part, descriptor);
    } else if (!spec.voxelRef_.empty()) {
        if (!manifest.voxels_[index]) {
            manifest.voxels_[index] = loadPartVoxels(spec.voxelRef_);
        }
        if (manifest.voxels_[index]) {
            attachVoxelContent(part, *manifest.voxels_[index], shapeChildren);
        } else {
            IRE_LOG_ERROR(
                "Prefab '{}': part '{}' voxel_ref load failed: {}",
                manifest.prefabId_.c_str(),
                spec.id_.c_str(),
                spec.voxelRef_.c_str()
            );
        }
    }

    for (const auto &[factory, fields] : spec.components_) {
        (*factory)(part, fields);
    }

    slot.pinned_.clear();
    if (!spec.resident_) {
        return;
    }
    slot.pinned_.reserve(shapeChildren.size() + 1);
    slot.pinned_.push_back(part);
    slot.pinned_.insert(slot.pinned_.end(), shapeChildren.begin(), shapeChildren.end());
    for (IREntity::EntityId content : slot.pinned_) {
        if (auto shape = IREntity::getComponentOptional<IRComponents::C_ShapeDescriptor>(content)) {
            (*shape)->lodMin_ = spec.lodMin_;
            (*shape)->lodMax_ = spec.lodMax_;
        }
        if (auto voxels = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(content)) {
            (*voxels)->lodMin_ = spec.lodMin_;
            (*voxels)->lodMax_ = spec.lodMax_;
        }
        IREntity::setComponent(content, IRComponents::C_LodTierOverride{tier});
    }
}

} // namespace

void registerPrefab(std::string id, std::string path) {
    auto &reg = registry();
    auto it = reg.find(id);
    if (it != reg.end()) {
        IRE_LOG_WARN(
            "Prefab.register: '{}' already registered (path='{}'); overwriting with '{}'",
            id.c_str(),
            it->second.c_str(),
            path.c_str()
        );
        it->second = std::move(path);
        return;
    }
    reg.emplace(std::move(id), std::move(path));
}

std::optional<std::string> prefabPath(std::string_view id) {
    auto &reg = registry();
    auto it = reg.find(std::string{id});
    if (it == reg.end()) {
        return std::nullopt;
    }
    return it->second;
}

void clearPrefabs() {
    registry().clear();
    partVoxelCache().clear();
}

ManifestResult readManifest(IRScript::LuaScript &script, const std::string &path) {
    ManifestResult result;
    sol::object root;
    try {
        sol::protected_function_result eval =
            script.lua().safe_script_file(path, sol::script_pass_on_error);
        if (!eval.valid()) {
            const sol::error error = eval;
            result.error_ = "file evaluation failed: " + std::string{error.what()};
            return result;
        }
        root = eval;
    } catch (const std::exception &error) {
        result.error_ = "file evaluation threw: " + std::string{error.what()};
        return result;
    }
    if (root.get_type() != sol::type::table) {
        result.error_ = "prefab file did not return a table";
        return result;
    }
    const sol::table prefab = root.as<sol::table>();
    const sol::optional<int> version = prefab["prefab_version"];
    if (!version || *version != kPrefabSchemaVersion) {
        result.error_ =
            "editor manifests require prefab_version = " + std::to_string(kPrefabSchemaVersion);
        return result;
    }
    if (sol::object rootLod = prefab["lod"];
        rootLod.valid() && rootLod.get_type() != sol::type::lua_nil) {
        result.error_ = "root-level lod is not supported; give each part its own lod band";
        return result;
    }

    std::shared_ptr<PartsManifest> manifest;
    if (auto error = parseParts(prefab, path, manifest)) {
        result.error_ = *error;
        return result;
    }
    PrefabDescription description;
    if (manifest) {
        description.parts_.reserve(manifest->parts_.size());
        for (const PartSpec &spec : manifest->parts_) {
            PrefabPartDescription part;
            part.id_ = spec.id_;
            part.voxelRef_ = spec.voxelRef_;
            part.shape_ = spec.shape_;
            part.transform_ = spec.transform_;
            part.rotationMode_ = spec.rotationMode_;
            part.canvasSize_ = spec.canvasSize_;
            part.lodMin_ = spec.lodMin_;
            part.lodMax_ = spec.lodMax_;
            part.resident_ = spec.resident_;
            description.parts_.push_back(std::move(part));
        }
    }
    result.description_ = std::move(description);
    return result;
}

std::optional<std::string>
writeManifest(const std::string &path, const PrefabDescription &description) {
    if (description.version_ != kPrefabSchemaVersion) {
        return "writer only supports prefab_version = " + std::to_string(kPrefabSchemaVersion);
    }
    std::unordered_set<std::string> ids;
    for (const PrefabPartDescription &part : description.parts_) {
        if (part.id_.empty()) {
            return std::string{"part id must not be empty"};
        }
        if (!ids.insert(part.id_).second) {
            return "duplicate part id '" + part.id_ + "'";
        }
        if (!part.voxelRef_.empty() && part.shape_) {
            return "part '" + part.id_ + "' takes voxel_ref or shape, not both";
        }
        if (part.voxelRef_.empty() && !part.shape_) {
            return "part '" + part.id_ + "' needs voxel_ref or shape";
        }
        if (part.lodMax_ > part.lodMin_) {
            return "part '" + part.id_ + "' has an inverted lod band";
        }
        if (IRPrefab::RotationMode::ownsEntityCanvas(part.rotationMode_) &&
            (part.canvasSize_.x <= 0 || part.canvasSize_.y <= 0)) {
            return "part '" + part.id_ + "' needs a positive canvas_size";
        }
    }

    const std::filesystem::path outputPath(path);
    if (const std::filesystem::path parent = outputPath.parent_path(); !parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
            return "could not create directory '" + parent.string() + "': " + error.message();
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "could not open '" + path + "' for writing";
    }
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "return {\n  prefab_version = " << description.version_ << ",\n  parts = {\n";
    for (const PrefabPartDescription &part : description.parts_) {
        out << "    {\n      id = " << luaString(part.id_) << ",\n";
        if (!part.voxelRef_.empty()) {
            out << "      voxel_ref = " << luaString(part.voxelRef_) << ",\n";
        } else if (part.shape_) {
            out << "      shape = { type = " << static_cast<int>(part.shape_->type_)
                << ", params = ";
            writeVec4(out, part.shape_->params_);
            out << ", color = ";
            writeColor(out, part.shape_->color_);
            out << ", flags = " << part.shape_->flags_ << " },\n";
        }
        out << "      transform = { translation = ";
        writeVec3(out, part.transform_.translation_);
        out << ", rotation = ";
        writeVec4(out, part.transform_.rotation_);
        out << ", scale = ";
        writeVec3(out, part.transform_.scale_);
        out << " },\n      rotation_mode = " << static_cast<int>(part.rotationMode_) << ",\n";
        if (IRPrefab::RotationMode::ownsEntityCanvas(part.rotationMode_)) {
            out << "      canvas_size = { x = " << part.canvasSize_.x
                << ", y = " << part.canvasSize_.y << " },\n";
        }
        out << "      lod = { fine = " << static_cast<int>(part.lodMax_)
            << ", coarse = " << static_cast<int>(part.lodMin_) << " },\n";
        if (part.resident_) {
            out << "      resident = true,\n";
        }
        out << "    },\n";
    }
    out << "  },\n}\n";
    if (!out) {
        return "failed while writing '" + path + "'";
    }
    return std::nullopt;
}

SpawnResult spawnPrefab(IRScript::LuaScript &script, std::string_view id, IRMath::vec3 position) {
    std::string idStr{id};
    auto pathOpt = prefabPath(id);
    if (!pathOpt) {
        return makeError(idStr, std::string{}, "no prefab registered for this id");
    }
    const std::string path = *pathOpt;

    // Evaluate the prefab file. With `SOL_ALL_SAFETIES_ON` set on this
    // target, `script_file` returns a `protected_function_result`
    // rather than throwing — but the file-open path can still throw
    // for missing files, so the try/catch is load-bearing.
    sol::object root;
    try {
        sol::protected_function_result eval = script.lua().script_file(path);
        if (!eval.valid()) {
            sol::error err = eval;
            return makeError(idStr, path, std::string{"file evaluation failed: "} + err.what());
        }
        root = eval;
    } catch (const std::exception &e) {
        return makeError(idStr, path, std::string{"file evaluation threw: "} + e.what());
    }

    if (root.get_type() != sol::type::table) {
        return makeError(idStr, path, "prefab file did not return a table");
    }
    sol::table prefab = root.as<sol::table>();

    sol::optional<int> versionOpt = prefab["prefab_version"];
    if (!versionOpt) {
        return makeError(idStr, path, "prefab_version field missing or not an integer");
    }
    if (*versionOpt < kPrefabSchemaVersionMin || *versionOpt > kPrefabSchemaVersion) {
        return makeError(
            idStr,
            path,
            "prefab_version=" + std::to_string(*versionOpt) + " not supported (expected " +
                std::to_string(kPrefabSchemaVersionMin) + ".." +
                std::to_string(kPrefabSchemaVersion) + ")"
        );
    }

    // Parsed before the root exists, so a bad part leaves nothing to tear down.
    std::shared_ptr<PartsManifest> partsManifest;
    if (*versionOpt >= 2) {
        // A band belongs to a part; a root-level table would be silently dropped.
        if (sol::object rootLod = prefab["lod"];
            rootLod.valid() && rootLod.get_type() != sol::type::lua_nil) {
            return makeError(
                idStr,
                path,
                "root-level lod is not supported; give each part its own lod band"
            );
        }
        if (auto error = parseParts(prefab, idStr, partsManifest)) {
            return makeError(idStr, path, *error);
        }
    } else if (
        sol::object parts = prefab["parts"]; parts.valid() && parts.get_type() != sol::type::lua_nil
    ) {
        IRE_LOG_WARN(
            "Prefab.spawn('{}'): parts needs prefab_version = 2; ignored under version 1.",
            idStr.c_str()
        );
    }

    sol::optional<std::string> voxelRef = prefab["voxel_ref"];
    std::optional<IRAsset::VoxelSetAllFile> loadedVoxels;
    if (voxelRef) {
        auto loadResult = IRAsset::loadVoxelSet(*voxelRef);
        if (!loadResult.ok()) {
            return makeError(idStr, path, std::string{"voxel_ref load failed: "} + *voxelRef);
        }
        loadedVoxels = std::move(loadResult.value_);
    }

    // Default rotation mode is GRID. A canvas-owning mode allocates a
    // per-entity canvas via `IRPrefab::EntityCanvas::create` so the C3
    // composite pass can thread `C_LocalTransform` through the per-canvas
    // TRS without re-rasterizing voxels per frame.
    IRComponents::RotationMode rotationMode = IRComponents::RotationMode::GRID;
    IRMath::ivec2 canvasSize{0};
    if (auto error = parseRotationMode(prefab, rotationMode, canvasSize)) {
        return makeError(idStr, path, *error);
    }

    bool unbounded = false;
    if (sol::optional<bool> unboundedOpt = prefab["unbounded"]; unboundedOpt) {
        unbounded = *unboundedOpt;
    }
    if (unbounded && !IRPrefab::RotationMode::ownsEntityCanvas(rotationMode)) {
        IRE_LOG_WARN(
            "Prefab.spawn('{}'): unbounded=true has no effect with "
            "rotation_mode=IRComponent.RotationMode.GRID.",
            idStr.c_str()
        );
    }

    // Optional rig_ref — load and translate to C_JointHierarchy via the
    // existing prefab-side bridge.
    sol::optional<std::string> rigRef = prefab["rig_ref"];
    std::optional<IRAsset::Rig> loadedRig;
    if (rigRef) {
        // loadRig takes (name, path). The prefab schema gives one
        // string — split into (basename, directory) so the loader
        // composes the right `<path>/<name>.rig`. Strip a trailing
        // `.rig` from the basename since `loadRig` appends it.
        std::string rigPath = *rigRef;
        std::string::size_type slash = rigPath.find_last_of("/\\");
        std::string dir =
            (slash == std::string::npos) ? std::string{"."} : rigPath.substr(0, slash);
        std::string base = (slash == std::string::npos) ? rigPath : rigPath.substr(slash + 1);
        constexpr std::string_view kExt{".rig"};
        if (base.size() >= kExt.size() &&
            base.compare(base.size() - kExt.size(), kExt.size(), kExt) == 0) {
            base = base.substr(0, base.size() - kExt.size());
        }
        auto rigResult = IRAsset::loadRig(base, dir);
        if (!rigResult.ok()) {
            return makeError(idStr, path, std::string{"rig_ref load failed: "} + rigPath);
        }
        loadedRig = std::move(rigResult.value_);
    }

    // Create the entity. The caller-supplied C_LocalTransform plus the
    // auto-added C_WorldTransform arrive in one createEntity call; the
    // joint hierarchy is set on the resulting entity (setComponent
    // migrates the archetype once, which is fine for spawn — it isn't
    // in a tick). The world transform starts equal to the local one, as
    // PROPAGATE_TRANSFORM computes it for a root, so content seeded from it
    // draws in place before the first propagation.
    const IRComponents::C_LocalTransform rootLocal{position};
    const IREntity::EntityId entity = IREntity::createEntity(
        rootLocal,
        IRComponents::C_WorldTransform{
            rootLocal.translation_,
            rootLocal.rotation_,
            rootLocal.scale_
        }
    );

    // C_RotationMode is always attached so archetype-filtered systems
    // (C3 composite, C6 grid rebuild) iterate prefab entities without
    // per-entity `getComponentOptional`. Non-prefab entities stay
    // implicitly GRID.
    IREntity::setComponent(entity, IRComponents::C_RotationMode{rotationMode});

    if (unbounded) {
        // Auto-attached by createEntity above; mutate the existing column
        // entry instead of replacing the whole component.
        IREntity::getComponent<IRComponents::C_LocalTransform>(entity).unbounded_ = true;
    }

    attachEntityCanvas(entity, rotationMode, idStr + "_canvas", canvasSize, idStr);
    // Setup can move a shape child out of the root's tree before it fails, so
    // the error paths also mark each shape the spawn created as its own tree.
    // The drain skips an entity already destroyed through another mark.
    std::vector<IREntity::EntityId> spawnedChildren;
    auto destroySpawned = [&]() {
        IREntity::destroyTree(entity);
        for (IREntity::EntityId child : spawnedChildren) {
            if (IREntity::entityExists(child)) {
                IREntity::destroyTree(child);
            }
        }
    };

    if (loadedRig) {
        IREntity::setComponent(entity, IRPrefab::Rig::toComponent(*loadedRig));
    }

    // Attach C_BindPoints from the loaded rig's BIND chunk; apply any bind_point_overrides on top.
    // Overrides only take effect when the rig's BIND chunk is non-empty (the guard below).
    if (loadedRig && !loadedRig->bindPoints_.empty()) {
        IRComponents::C_BindPoints bindPoints = IRPrefab::Rig::toBindPoints(*loadedRig);
        sol::optional<sol::table> overridesOpt = prefab["bind_point_overrides"];
        if (overridesOpt) {
            for (auto &kv : *overridesOpt) {
                sol::optional<std::string> nameOpt = kv.first.as<sol::optional<std::string>>();
                if (!nameOpt) {
                    continue;
                }
                if (kv.second.get_type() != sol::type::table) {
                    continue;
                }
                sol::table desc = kv.second.as<sol::table>();
                auto existing = bindPoints.points_.find(*nameOpt);
                IRComponents::BindPointRuntime point = (existing != bindPoints.points_.end())
                                                           ? existing->second
                                                           : IRComponents::BindPointRuntime{};
                sol::optional<std::uint32_t> boneIdOpt = desc["boneId"];
                if (boneIdOpt) {
                    point.boneId_ = *boneIdOpt;
                }
                sol::optional<IRMath::vec3> offsetOpt = desc["offset"];
                if (offsetOpt) {
                    point.offset_ = *offsetOpt;
                }
                sol::optional<IRMath::vec4> rotationOpt = desc["rotation"];
                if (rotationOpt) {
                    point.rotation_ = *rotationOpt;
                }
                bindPoints.points_[*nameOpt] = point;
            }
        }
        IREntity::setComponent(entity, std::move(bindPoints));
    }

    if (loadedVoxels) {
        attachVoxelContent(entity, *loadedVoxels, spawnedChildren);
    }

    // Optional declarative `components = { C_Foo = { field = ... }, ... }`
    // block. Each entry's factory (registered by the component's
    // `*_lua.hpp` via `IRScript::registerComponentFactoryFor`) builds
    // the component from the override table and attaches it. Runs
    // before the parts so a declared C_LodTierOverride pin decides which
    // parts spawn, and before `setup` so the callback observes the
    // declarative components and may freely overwrite or extend them.
    std::vector<std::pair<const ComponentFactory *, sol::table>> rootComponents;
    if (auto error = parseComponents(prefab, rootComponents)) {
        destroySpawned();
        return makeError(idStr, path, *error);
    }
    for (const auto &[factory, fields] : rootComponents) {
        (*factory)(entity, fields);
    }

    if (partsManifest) {
        IRComponents::C_PrefabParts parts;
        parts.slots_.resize(partsManifest->parts_.size());
        const IRRender::LodLevel tier = resolveSpawnTier(entity);
        parts.tier_ = tier;
        parts.candidateTier_ = tier;
        parts.candidateTicks_ = IRConstants::kPrefabPartsTierSettleTicks;
        for (std::size_t i = 0; i < parts.slots_.size(); ++i) {
            IRComponents::PrefabPartSlot &slot = parts.slots_[i];
            const PartSpec &spec = partsManifest->parts_[i];
            slot.lodMin_ = spec.lodMin_;
            slot.lodMax_ = spec.lodMax_;
            slot.resident_ = spec.resident_;
            if (!slot.inBand(tier)) {
                continue;
            }
            slot.entity_ = IREntity::createEntity();
            buildPart(entity, slot.entity_, *partsManifest, i, tier, slot);
        }
        parts.manifest_ = std::move(partsManifest);
        IREntity::setComponent(entity, std::move(parts));
    }

    // Optional setup function — last so the user sees a fully-formed
    // entity (position + rig + bind points + shape children + declared
    // components + parts already attached). Distinguish "absent"
    // (sol::type::lua_nil) from "present but not a function" so a
    // schema typo like `setup = 42` surfaces a diagnostic instead of
    // silently no-op'ing.
    sol::object setupObj = prefab["setup"];
    if (setupObj.valid() && setupObj.get_type() != sol::type::lua_nil) {
        if (setupObj.get_type() != sol::type::function) {
            destroySpawned();
            return makeError(idStr, path, "setup must be a function");
        }
        sol::protected_function setupFn = setupObj.as<sol::protected_function>();
        sol::protected_function_result setupResult = setupFn(IRScript::LuaEntity{entity});
        if (!setupResult.valid()) {
            sol::error err = setupResult;
            destroySpawned();
            return makeError(idStr, path, std::string{"setup callback failed: "} + err.what());
        }
    }

    SpawnResult r;
    r.entity_ = entity;
    r.id_ = std::move(idStr);
    r.path_ = path;
    return r;
}

void stagePartSpawn(
    IREntity::EntityId root, IRComponents::C_PrefabParts &parts, std::size_t index
) {
    IREntity::EntityManager &entityManager = IREntity::getEntityManager();
    const IREntity::EntityId part = entityManager.createEntityDeferred();
    parts.slots_[index].entity_ = part;
    entityManager.stageStructuralChange(
        [root, part, index, tier = parts.tier_, manifest = parts.manifest_]() {
            // A root marked for deletion since staging gets no part: a tree
            // mark already listed its descendants, so the part would outlive it.
            auto rootParts = IREntity::entityExists(root) &&
                                     !IREntity::getEntityManager().isMarkedForDeletion(root)
                                 ? IREntity::getComponentOptional<IRComponents::C_PrefabParts>(root)
                                 : std::nullopt;
            if (!rootParts || (*rootParts)->slots_[index].entity_ != part) {
                IREntity::destroyEntity(part);
                return;
            }
            IRComponents::PrefabPartSlot built;
            buildPart(root, part, *manifest, index, tier, built);
            // buildPart moved archetypes, so the root's column may have moved.
            IRComponents::PrefabPartSlot &slot =
                IREntity::getComponent<IRComponents::C_PrefabParts>(root).slots_[index];
            slot.pinned_ = std::move(built.pinned_);
        }
    );
}

void despawnPart(IRComponents::PrefabPartSlot &slot) {
    IREntity::destroyTree(slot.entity_);
    slot.entity_ = IREntity::kNullEntity;
    slot.pinned_.clear();
}

void pinResidentPart(const IRComponents::PrefabPartSlot &slot, IRRender::LodLevel tier) {
    for (IREntity::EntityId content : slot.pinned_) {
        if (auto pin = IREntity::getComponentOptional<IRComponents::C_LodTierOverride>(content)) {
            (*pin)->tier_ = tier;
        }
    }
}

} // namespace IRPrefab::Prefab
