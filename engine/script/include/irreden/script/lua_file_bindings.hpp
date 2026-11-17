#ifndef LUA_FILE_BINDINGS_H
#define LUA_FILE_BINDINGS_H

#include <irreden/script/lua_script.hpp>
#include <irreden/utility/path_utils.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace IRScript::detail {

inline std::optional<std::filesystem::path>
resolveRunPath(const std::filesystem::path &runDirectory, const std::string &path) {
    return IRUtility::resolveContainedPath(runDirectory, path);
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

    // Reads also accept an absolute path the script's grants hold (what the
    // user picked in IRDialog.open); writes stay inside the root. The grant
    // set is a LuaScript member that outlives these closures.
    const PathGrants *grants = &script.pathGrants();

    files["readText"] = [resolvedRunDirectory,
                         grants](const std::string &path) -> sol::optional<std::string> {
        const auto resolved = resolveReadablePath(*grants, resolvedRunDirectory, path);
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
    files["mtime"] = [resolvedRunDirectory, grants](const std::string &path) {
        const auto resolved = resolveReadablePath(*grants, resolvedRunDirectory, path);
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
    files["list"] =
        [resolvedRunDirectory,
         grants](const std::string &path, sol::this_state state) -> sol::optional<sol::table> {
        const auto resolved = resolveReadablePath(*grants, resolvedRunDirectory, path);
        if (!resolved) {
            return sol::nullopt;
        }
        std::error_code listError;
        std::filesystem::directory_iterator entries{*resolved, listError};
        if (listError) {
            return sol::nullopt;
        }
        std::vector<std::pair<std::string, bool>> found;
        for (const std::filesystem::directory_iterator end; entries != end;
             entries.increment(listError)) {
            if (listError) {
                return sol::nullopt;
            }
            std::error_code kindError;
            const std::u8string name = entries->path().filename().u8string();
            found.emplace_back(
                std::string{name.begin(), name.end()},
                entries->is_directory(kindError)
            );
        }
        std::ranges::sort(found);
        sol::state_view view{state};
        sol::table out = view.create_table(static_cast<int>(found.size()), 0);
        for (std::size_t i = 0; i < found.size(); ++i) {
            sol::table entry = view.create_table(0, 2);
            entry["name"] = found[i].first;
            entry["directory"] = found[i].second;
            out[i + 1] = entry;
        }
        return out;
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
