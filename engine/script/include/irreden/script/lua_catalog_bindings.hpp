#ifndef LUA_CATALOG_BINDINGS_H
#define LUA_CATALOG_BINDINGS_H

#include <irreden/ir_catalog.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace IRScript::detail {

inline const char *creationKindName(IRAsset::CreationKind kind) {
    switch (kind) {
    case IRAsset::CreationKind::DEMO:
        return "demo";
    case IRAsset::CreationKind::EDITOR:
        return "editor";
    case IRAsset::CreationKind::GAME:
        return "game";
    case IRAsset::CreationKind::TOOL:
        return "tool";
    case IRAsset::CreationKind::TEST:
        return "test";
    }
    throw std::invalid_argument("unknown creation kind");
}

inline const char *assetKindName(IRCatalog::AssetKind kind) {
    switch (kind) {
    case IRCatalog::AssetKind::VOXEL_SET:
        return "voxel_set";
    case IRCatalog::AssetKind::PREFAB:
        return "prefab";
    case IRCatalog::AssetKind::PNG:
        return "png";
    case IRCatalog::AssetKind::DATA:
        return "data";
    }
    throw std::invalid_argument("unknown asset kind");
}

inline sol::table stringsToLua(sol::state_view lua, const std::vector<std::string> &strings) {
    sol::table result = lua.create_table(static_cast<int>(strings.size()), 0);
    for (std::size_t index = 0; index < strings.size(); ++index) {
        result[static_cast<lua_Integer>(index + 1)] = strings[index];
    }
    return result;
}

inline sol::table manifestToLua(sol::state_view lua, const IRAsset::CreationManifest &manifest) {
    sol::table result = lua.create_table(0, 9);
    result["version"] = manifest.version_;
    result["name"] = manifest.name_;
    result["description"] = manifest.description_;
    result["group"] = manifest.group_;
    result["tags"] = stringsToLua(lua, manifest.tags_);
    result["kind"] = creationKindName(manifest.kind_);
    result["thumbnail"] = manifest.thumbnail_;
    result["docs"] = stringsToLua(lua, manifest.docs_);

    sol::table targets = lua.create_table(static_cast<int>(manifest.targets_.size()), 0);
    for (std::size_t targetIndex = 0; targetIndex < manifest.targets_.size(); ++targetIndex) {
        const auto &target = manifest.targets_[targetIndex];
        sol::table targetTable = lua.create_table(0, 5);
        targetTable["name"] = target.name_;
        targetTable["display"] = target.display_;
        targetTable["description"] = target.description_;
        targetTable["package"] = target.package_;
        sol::table presets = lua.create_table(static_cast<int>(target.presets_.size()), 0);
        for (std::size_t presetIndex = 0; presetIndex < target.presets_.size(); ++presetIndex) {
            sol::table preset = lua.create_table(0, 2);
            preset["name"] = target.presets_[presetIndex].name_;
            preset["args"] = stringsToLua(lua, target.presets_[presetIndex].args_);
            presets[static_cast<lua_Integer>(presetIndex + 1)] = preset;
        }
        targetTable["presets"] = presets;
        targets[static_cast<lua_Integer>(targetIndex + 1)] = targetTable;
    }
    result["targets"] = targets;
    return result;
}

inline sol::table verdictToLua(sol::state_view lua, const IRCatalog::RunVerdict &verdict) {
    sol::table result = lua.create_table(0, 6);
    result["verdict"] = verdict.verdict_;
    result["exe"] = verdict.exe_;
    if (verdict.exit_) {
        result["exit"] = *verdict.exit_;
    }
    if (verdict.signal_) {
        result["signal"] = *verdict.signal_;
    }
    result["argv"] = stringsToLua(lua, verdict.argv_);
    result["at"] = verdict.at_;
    return result;
}

inline sol::table catalogToLua(sol::state_view lua, const IRCatalog::Catalog &catalog) {
    sol::table result = lua.create_table(0, 1);
    sol::table creations = lua.create_table(static_cast<int>(catalog.creations_.size()), 0);
    for (std::size_t creationIndex = 0; creationIndex < catalog.creations_.size();
         ++creationIndex) {
        const auto &creation = catalog.creations_[creationIndex];
        sol::table creationTable = lua.create_table(0, 5);
        creationTable["root"] = creation.root_;
        creationTable["dir"] = creation.dir_;
        if (creation.manifest_) {
            creationTable["manifest"] = manifestToLua(lua, *creation.manifest_);
        }
        if (creation.error_) {
            sol::table error = lua.create_table(0, 4);
            error["line"] = creation.error_->line_;
            error["column"] = creation.error_->column_;
            error["path"] = creation.error_->path_;
            error["message"] = creation.error_->message_;
            creationTable["error"] = error;
        }

        sol::table targets = lua.create_table(static_cast<int>(creation.targets_.size()), 0);
        for (std::size_t targetIndex = 0; targetIndex < creation.targets_.size(); ++targetIndex) {
            const auto &target = creation.targets_[targetIndex];
            sol::table targetTable = lua.create_table(0, 7);
            targetTable["name"] = target.name_;
            targetTable["display"] = target.display_;
            targetTable["built"] = target.built_;
            targetTable["exePath"] = target.exePath_;
            targetTable["mtime"] = target.mtime_;
            targetTable["packagePath"] = target.packagePath_;
            if (target.lastRun_) {
                targetTable["lastRun"] = verdictToLua(lua, *target.lastRun_);
            }
            targets[static_cast<lua_Integer>(targetIndex + 1)] = targetTable;
        }
        creationTable["targets"] = targets;
        creations[static_cast<lua_Integer>(creationIndex + 1)] = creationTable;
    }
    result["creations"] = creations;
    return result;
}

inline sol::table assetIndexToLua(sol::state_view lua, const IRCatalog::AssetIndex &index) {
    sol::table result = lua.create_table(0, 1);
    sol::table entries = lua.create_table(static_cast<int>(index.entries_.size()), 0);
    for (std::size_t indexValue = 0; indexValue < index.entries_.size(); ++indexValue) {
        const auto &entry = index.entries_[indexValue];
        sol::table entryTable = lua.create_table(0, 5);
        entryTable["kind"] = assetKindName(entry.kind_);
        entryTable["path"] = entry.path_;
        entryTable["mtime"] = entry.mtime_;
        sol::table fields = lua.create_table(0, static_cast<int>(entry.fields_.size()));
        for (const auto &[name, value] : entry.fields_) {
            fields[name] = value;
        }
        entryTable["fields"] = fields;
        entryTable["parts"] = stringsToLua(lua, entry.parts_);
        entries[static_cast<lua_Integer>(indexValue + 1)] = entryTable;
    }
    result["entries"] = entries;
    return result;
}

inline std::string
option(sol::optional<sol::table> options, const char *name, const char *fallback) {
    if (!options) {
        return fallback;
    }
    sol::object value = (*options)[name];
    if (!value.valid() || value.get_type() == sol::type::lua_nil) {
        return fallback;
    }
    if (value.get_type() != sol::type::string) {
        throw std::invalid_argument(std::string("IRCatalog ") + name + " must be a string");
    }
    return value.as<std::string>();
}

inline void bindCatalog(LuaScript &script, const std::filesystem::path &toolRoot) {
    auto service = std::make_shared<IRCatalog::CatalogService>(toolRoot);
    sol::state &lua = script.lua();
    sol::object existing = lua["IRCatalog"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRCatalog must be a table before bindLuaCatalog()");
    }
    sol::table table =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    table["scan"] =
        statefulLuaFunction([&script, service](sol::optional<sol::table> options) -> sol::table {
            const auto root = option(options, "root", ".");
            const auto buildDir = option(options, "buildDir", "build");
            return catalogToLua(
                sol::state_view{script.lua().lua_state()},
                service->scan(root, buildDir)
            );
        });
    table["assets"] =
        statefulLuaFunction([&script, service](sol::optional<sol::table> options) -> sol::table {
            const auto root = option(options, "root", ".");
            return assetIndexToLua(
                sol::state_view{script.lua().lua_state()},
                service->assets(root)
            );
        });
    lua["IRCatalog"] = table;
}

} // namespace IRScript::detail

#endif // LUA_CATALOG_BINDINGS_H
