#include <irreden/script/prefab_api.hpp>

#include <irreden/asset/rig_format.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/voxel/rig_bridge.hpp>

#include <sol/sol.hpp>

#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRPrefab::Prefab {

namespace {

/// In-process registry. Module-local; no cross-process or cross-World
/// sharing. Mirrors the lifetime story of `IRPrefab::Modifier`'s
/// `globalFieldRegistry()` — process-singleton, cleared by tests via
/// `clearPrefabs()`.
std::unordered_map<std::string, std::string> &registry() {
    static std::unordered_map<std::string, std::string> g_registry;
    return g_registry;
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

    if (!root.is<sol::table>()) {
        return makeError(idStr, path, "prefab file did not return a table");
    }
    sol::table prefab = root.as<sol::table>();

    // Schema version. Required; missing → bad-format; mismatched →
    // VersionTooNew/Old style diagnostic.
    sol::optional<int> versionOpt = prefab["prefab_version"];
    if (!versionOpt) {
        return makeError(idStr, path, "prefab_version field missing or not an integer");
    }
    if (*versionOpt != kPrefabSchemaVersion) {
        return makeError(
            idStr,
            path,
            "prefab_version=" + std::to_string(*versionOpt) + " not supported (expected " +
                std::to_string(kPrefabSchemaVersion) + ")"
        );
    }

    // Optional voxel_ref — SHAPES records attach as child entities (one
    // per record) below; DENSE data is loaded but not attached in v1
    // (C_VoxelSetNew requires an active render-canvas pool — the
    // headless attach path is deferred; see CLAUDE.md "Prefab format").
    sol::optional<std::string> voxelRef = prefab["voxel_ref"];
    std::optional<IRAsset::VoxelSetAllFile> loadedVoxels;
    if (voxelRef) {
        auto loadResult = IRAsset::loadVoxelSet(*voxelRef);
        if (!loadResult.ok()) {
            return makeError(idStr, path, std::string{"voxel_ref load failed: "} + *voxelRef);
        }
        loadedVoxels = std::move(loadResult.value_);
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

    // Create the entity. C_Position3D + the auto-added position
    // components arrive in one createEntity call; the joint hierarchy
    // is set on the resulting entity (setComponent migrates the
    // archetype once, which is fine for spawn — it isn't in a tick).
    const IREntity::EntityId entity = IREntity::createEntity(IRComponents::C_Position3D{position});

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
                if (!kv.second.is<sol::table>()) {
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

    // SHAPES voxel_ref attachment — one child entity per ShapeRecord,
    // CHILD_OF the spawned root so per-record `offset_` composes
    // through the standard C_Position3D + parent's C_PositionGlobal3D
    // path (any modifier-driven offset on the parent is already baked
    // into globalPos by APPLY_POSITION_OFFSET). Per-record
    // `rotation_`, `csgOp_`, and `boneId_` are
    // persisted but not consumed by the current renderer; loading them
    // is a no-op until a runtime system reads them (T-181 wires bone
    // bindings via C_BindPoints above; CSG composition is a render-side
    // decision). v1 intentionally drops these to avoid stamping unused
    // state.
    //
    // DENSE / HYBRID dense data attaches as `C_VoxelSetNew` on the
    // root entity via `IRPrefab::DenseVoxel::toComponent` — the
    // adapter routes through the dense-data ctor, which is
    // headless-safe: with an active canvas it allocates from the
    // pool and seeds positions; without one it stages records in
    // `pendingVoxels_` for a later canvas-attach pass.
    std::vector<IREntity::EntityId> spawnedChildren;
    if (loadedVoxels && (loadedVoxels->mode_ == IRAsset::VoxelSetMode::SHAPES ||
                         loadedVoxels->mode_ == IRAsset::VoxelSetMode::HYBRID)) {
        spawnedChildren.reserve(loadedVoxels->shapeRecords_.size());
        for (const auto &record : loadedVoxels->shapeRecords_) {
            IRComponents::C_ShapeDescriptor descriptor{
                static_cast<IRMath::SDF::ShapeType>(record.shapeTypeId_),
                record.params_,
                record.color_
            };
            descriptor.flags_ = record.flags_;
            const IREntity::EntityId child =
                IREntity::createEntity(IRComponents::C_Position3D{record.offset_}, descriptor);
            IREntity::setParent(child, entity);
            spawnedChildren.push_back(child);
        }
    }

    if (loadedVoxels && (loadedVoxels->mode_ == IRAsset::VoxelSetMode::DENSE ||
                         loadedVoxels->mode_ == IRAsset::VoxelSetMode::HYBRID)) {
        IRComponents::C_VoxelSetNew voxelSet =
            IRPrefab::DenseVoxel::toComponent(loadedVoxels->dense_);
        if (voxelSet.recordCount() > 0) {
            IREntity::setComponent(entity, std::move(voxelSet));
        }
    }

    // Optional declarative `components = { C_Foo = { field = ... }, ... }`
    // block. Each entry's factory (registered by the component's
    // `*_lua.hpp` via `IRScript::registerComponentFactoryFor`) builds
    // the component from the override table and attaches it. Runs
    // before `setup` so the callback observes the declarative
    // components and may freely overwrite or extend them.
    sol::optional<sol::table> componentsOpt = prefab["components"];
    if (componentsOpt) {
        for (auto &kv : *componentsOpt) {
            sol::optional<std::string> nameOpt = kv.first.as<sol::optional<std::string>>();
            if (!nameOpt) {
                for (auto child : spawnedChildren) {
                    IREntity::destroyEntity(child);
                }
                IREntity::destroyEntity(entity);
                return makeError(idStr, path, "components keys must be component-name strings");
            }
            if (!kv.second.is<sol::table>()) {
                for (auto child : spawnedChildren) {
                    IREntity::destroyEntity(child);
                }
                IREntity::destroyEntity(entity);
                return makeError(
                    idStr,
                    path,
                    std::string{"components['"} + *nameOpt +
                        "'] must be a table of field overrides"
                );
            }
            const ComponentFactory *factory = findComponentFactory(*nameOpt);
            if (!factory) {
                for (auto child : spawnedChildren) {
                    IREntity::destroyEntity(child);
                }
                IREntity::destroyEntity(entity);
                return makeError(
                    idStr,
                    path,
                    std::string{"no factory registered for component '"} + *nameOpt +
                        "' (the binding's *_lua.hpp must call "
                        "IRScript::registerComponentFactoryFor and the creation must include it)"
                );
            }
            sol::table fields = kv.second.as<sol::table>();
            (*factory)(entity, fields);
        }
    }

    // Optional setup function — last so the user sees a fully-formed
    // entity (position + rig + bind points + shape children + declared
    // components already attached). Distinguish "absent"
    // (sol::type::lua_nil) from "present but not a function" so a
    // schema typo like `setup = 42` surfaces a diagnostic instead of
    // silently no-op'ing.
    sol::object setupObj = prefab["setup"];
    if (setupObj.valid() && setupObj.get_type() != sol::type::lua_nil) {
        if (setupObj.get_type() != sol::type::function) {
            for (auto child : spawnedChildren) {
                IREntity::destroyEntity(child);
            }
            IREntity::destroyEntity(entity);
            return makeError(idStr, path, "setup must be a function");
        }
        sol::protected_function setupFn = setupObj.as<sol::protected_function>();
        sol::protected_function_result setupResult = setupFn(IRScript::LuaEntity{entity});
        if (!setupResult.valid()) {
            sol::error err = setupResult;
            for (auto child : spawnedChildren) {
                IREntity::destroyEntity(child);
            }
            IREntity::destroyEntity(entity);
            return makeError(idStr, path, std::string{"setup callback failed: "} + err.what());
        }
    }

    SpawnResult r;
    r.entity_ = entity;
    r.id_ = std::move(idStr);
    r.path_ = path;
    return r;
}

} // namespace IRPrefab::Prefab
