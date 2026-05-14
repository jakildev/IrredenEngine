#include <irreden/script/prefab_api.hpp>

#include <irreden/asset/rig_format.hpp>
#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/rig_bridge.hpp>

#include <sol/sol.hpp>

#include <exception>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

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
    IRE_LOG_ERROR(
        "Prefab.spawn('{}'): {} (path='{}')", id.c_str(), message.c_str(), path.c_str()
    );
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

SpawnResult
spawnPrefab(IRScript::LuaScript &script, std::string_view id, IRMath::vec3 position) {
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
            return makeError(
                idStr, path, std::string{"file evaluation failed: "} + err.what()
            );
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
            "prefab_version=" + std::to_string(*versionOpt) +
                " not supported (expected " + std::to_string(kPrefabSchemaVersion) + ")"
        );
    }

    // Optional voxel_ref — load + verify but defer attachment to a
    // follow-up task (C_VoxelSetNew requires an active canvas pool).
    sol::optional<std::string> voxelRef = prefab["voxel_ref"];
    if (voxelRef) {
        auto loadResult = IRAsset::loadVoxelSet(*voxelRef);
        if (!loadResult.ok()) {
            return makeError(
                idStr,
                path,
                std::string{"voxel_ref load failed: "} + *voxelRef
            );
        }
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
        std::string dir = (slash == std::string::npos) ? std::string{"."} : rigPath.substr(0, slash);
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
    const IREntity::EntityId entity =
        IREntity::createEntity(IRComponents::C_Position3D{position});

    if (loadedRig) {
        IREntity::setComponent(entity, IRPrefab::Rig::toComponent(*loadedRig));
    }

    // Optional setup function — last so the user sees a fully-formed
    // entity (position + rig already attached).
    sol::optional<sol::protected_function> setupFn = prefab["setup"];
    if (setupFn) {
        sol::protected_function_result setupResult = (*setupFn)(IRScript::LuaEntity{entity});
        if (!setupResult.valid()) {
            sol::error err = setupResult;
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
