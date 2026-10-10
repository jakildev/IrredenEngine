#ifndef LUA_FILE_BINDINGS_H
#define LUA_FILE_BINDINGS_H

#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace IRScript::detail {

inline bool
isWithinDirectory(const std::filesystem::path &directory, const std::filesystem::path &candidate) {
    auto directoryPart = directory.begin();
    auto candidatePart = candidate.begin();
    while (directoryPart != directory.end() && candidatePart != candidate.end()) {
        if (*directoryPart != *candidatePart) {
            return false;
        }
        ++directoryPart;
        ++candidatePart;
    }
    return directoryPart == directory.end();
}

inline std::optional<std::filesystem::path>
resolveRunPath(const std::filesystem::path &runDirectory, const std::string &path) {
    const std::filesystem::path relativePath(path);
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_name()) {
        return std::nullopt;
    }

    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(runDirectory / relativePath, error);
    if (error || !isWithinDirectory(runDirectory, resolved)) {
        return std::nullopt;
    }
    return resolved;
}

inline void bindFiles(LuaScript &script, const std::filesystem::path &runDirectory) {
    std::error_code error;
    const std::filesystem::path resolvedRunDirectory =
        std::filesystem::weakly_canonical(runDirectory, error);
    if (error) {
        throw std::invalid_argument("bindLuaFiles requires a valid run directory");
    }

    sol::state &lua = script.lua();
    sol::object existing = lua["IRFile"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRFile must be a table before bindLuaFiles()");
    }
    sol::table files =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    files["readText"] =
        [resolvedRunDirectory](const std::string &path) -> sol::optional<std::string> {
        const auto resolved = resolveRunPath(resolvedRunDirectory, path);
        if (!resolved) {
            return sol::nullopt;
        }
        std::ifstream input(*resolved, std::ios::binary);
        if (!input) {
            return sol::nullopt;
        }
        std::ostringstream text;
        text << input.rdbuf();
        if (input.bad()) {
            return sol::nullopt;
        }
        return text.str();
    };
    files["writeText"] = [resolvedRunDirectory](const std::string &path, const std::string &text) {
        const auto resolved = resolveRunPath(resolvedRunDirectory, path);
        if (!resolved) {
            return false;
        }
        std::error_code createError;
        std::filesystem::create_directories(resolved->parent_path(), createError);
        if (createError) {
            return false;
        }
        std::ofstream output(*resolved, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        return !output.fail();
    };
    files["mtime"] = [resolvedRunDirectory](const std::string &path) {
        const auto resolved = resolveRunPath(resolvedRunDirectory, path);
        if (!resolved) {
            return 0.0;
        }
        std::error_code timeError;
        const auto modified = std::filesystem::last_write_time(*resolved, timeError);
        if (timeError) {
            return 0.0;
        }
        const double timestamp = std::chrono::duration<double>(modified.time_since_epoch()).count();
        return timestamp == 0.0 ? 1.0 : timestamp;
    };

    lua["IRFile"] = files;
}

inline void bindFiles(LuaScript &script) {
    std::error_code error;
    const std::filesystem::path runDirectory = std::filesystem::current_path(error);
    if (error) {
        throw std::runtime_error("bindLuaFiles could not resolve the run directory");
    }
    bindFiles(script, runDirectory);
}

} // namespace IRScript::detail

#endif // LUA_FILE_BINDINGS_H
