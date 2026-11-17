#ifndef LUA_FILE_DIALOG_BINDINGS_H
#define LUA_FILE_DIALOG_BINDINGS_H

// The opt-in `IRDialog` table: the native open panel from Lua.
//
//   local r = IRDialog.open{
//       mode = IRDialog.Mode.OPEN_FILES,   -- OPEN_FILE (default) | OPEN_FOLDER
//       title = "Choose images",
//       directory = "data/images",         -- starting directory
//       filters = { { name = "Images", extensions = { "png", "jpg" } } },
//   }
//   -- r.status (IRDialog.Status.*), r.paths (absolute, empty unless ACCEPTED),
//   -- r.error (string or nil)
//
// Every returned path is granted to this script's confined readers: a picked
// file is readable by exactly that path, a picked folder by everything beneath
// it. Contract: docs/design/native-file-dialog.md.

#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/window/file_dialog.hpp>

#include <sol/sol.hpp>

#include <array>
#include <expected>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace IRScript::detail {

inline constexpr const char *kFileDialogCaller = "IRDialog.open";
inline constexpr std::array<std::string_view, 4> kFileDialogOptionKeys{
    "mode", "title", "directory", "filters"
};
inline constexpr std::array<std::string_view, 2> kFileDialogFilterKeys{"name", "extensions"};

[[noreturn]] inline void raiseFileDialog(const std::string &message) {
    throw sol::error{std::string(kFileDialogCaller) + ": " + message};
}

inline bool isAbsentOption(const sol::object &value) {
    return !value.valid() || value.get_type() == sol::type::lua_nil;
}

inline std::string fileDialogString(const sol::object &value, const std::string &what) {
    if (value.get_type() != sol::type::string) {
        raiseFileDialog(what + " must be a string");
    }
    return value.as<std::string>();
}

// The values of a dense 1-based array, in order; anything else raises.
inline std::vector<sol::object> fileDialogArray(const sol::object &value, const std::string &what) {
    if (value.get_type() != sol::type::table) {
        raiseFileDialog(what + " must be an array");
    }
    const sol::table table = value.as<sol::table>();
    std::size_t count = 0;
    for (const auto &entry : table) {
        const sol::object &key = entry.first;
        if (key.get_type() != sol::type::number) {
            raiseFileDialog(what + " must be an array; it has a non-integer key");
        }
        ++count;
    }
    std::vector<sol::object> values;
    values.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        sol::object element = table.raw_get<sol::object>(i);
        if (isAbsentOption(element)) {
            raiseFileDialog(what + " must be a dense array; it has holes");
        }
        values.push_back(std::move(element));
    }
    return values;
}

inline IRWindow::FileDialogMode fileDialogModeFromLua(const sol::object &value) {
    constexpr const char *kExpected =
        "mode must be an IRDialog.Mode value (IRDialog.Mode.OPEN_FILE, OPEN_FILES or OPEN_FOLDER)";
    if (value.get_type() != sol::type::number) {
        raiseFileDialog(kExpected);
    }
    const double number = value.as<double>();
    for (const IRWindow::FileDialogMode mode :
         {IRWindow::FileDialogMode::OPEN_FILE,
          IRWindow::FileDialogMode::OPEN_FILES,
          IRWindow::FileDialogMode::OPEN_FOLDER}) {
        if (number == static_cast<double>(mode)) {
            return mode;
        }
    }
    raiseFileDialog(kExpected);
}

inline IRWindow::FileDialogRequest fileDialogRequestFromLua(const sol::object &argument) {
    IRWindow::FileDialogRequest request;
    if (isAbsentOption(argument)) {
        return request;
    }
    if (argument.get_type() != sol::type::table) {
        raiseFileDialog("expects a table { mode, title, directory, filters }");
    }
    const sol::table options = argument.as<sol::table>();
    rejectUnknownKeys(options, kFileDialogOptionKeys, kFileDialogCaller);

    if (const sol::object mode = options["mode"]; !isAbsentOption(mode)) {
        request.mode_ = fileDialogModeFromLua(mode);
    }
    if (const sol::object title = options["title"]; !isAbsentOption(title)) {
        request.title_ = fileDialogString(title, "title");
    }
    if (const sol::object directory = options["directory"]; !isAbsentOption(directory)) {
        request.directory_ = fileDialogString(directory, "directory");
    }
    if (const sol::object filters = options["filters"]; !isAbsentOption(filters)) {
        std::size_t index = 0;
        for (const sol::object &entry : fileDialogArray(filters, "filters")) {
            const std::string label = "filters[" + std::to_string(++index) + "]";
            if (entry.get_type() != sol::type::table) {
                raiseFileDialog(label + " must be a table { name, extensions }");
            }
            const sol::table filterTable = entry.as<sol::table>();
            rejectUnknownKeys(filterTable, kFileDialogFilterKeys, kFileDialogCaller);
            IRWindow::FileDialogFilter filter;
            if (const sol::object name = filterTable["name"]; !isAbsentOption(name)) {
                filter.name_ = fileDialogString(name, label + ".name");
            }
            const sol::object extensions = filterTable["extensions"];
            if (isAbsentOption(extensions)) {
                raiseFileDialog(label + ".extensions must be an array of strings");
            }
            std::size_t extensionIndex = 0;
            for (const sol::object &extension :
                 fileDialogArray(extensions, label + ".extensions")) {
                filter.extensions_.push_back(fileDialogString(
                    extension,
                    label + ".extensions[" + std::to_string(++extensionIndex) + "]"
                ));
            }
            request.filters_.push_back(std::move(filter));
        }
    }

    std::expected<IRWindow::FileDialogRequest, std::string> normalized =
        IRWindow::normalizeFileDialogRequest(request);
    if (!normalized.has_value()) {
        raiseFileDialog(normalized.error());
    }
    return std::move(*normalized);
}

// Grants what the user picked and drops any path that no longer resolves, so
// every path Lua receives is one its confined readers accept. Anything but an
// accepted pick carries no paths and grants nothing.
inline void grantFileDialogPaths(
    PathGrants &grants, IRWindow::FileDialogMode mode, IRWindow::FileDialogResult &result
) {
    if (result.status_ != IRWindow::FileDialogStatus::ACCEPTED) {
        result.paths_.clear();
        return;
    }
    std::vector<std::string> granted;
    granted.reserve(result.paths_.size());
    for (std::string &path : result.paths_) {
        const std::filesystem::path picked{std::u8string{path.begin(), path.end()}};
        const bool ok = mode == IRWindow::FileDialogMode::OPEN_FOLDER
                            ? grants.grantDirectory(picked)
                            : grants.grantFile(picked);
        if (ok) {
            granted.push_back(std::move(path));
        }
    }
    const std::size_t dropped = result.paths_.size() - granted.size();
    result.paths_ = std::move(granted);
    if (dropped == 0) {
        return;
    }
    result.error_ = std::to_string(dropped) + " picked path(s) no longer resolve";
    if (result.paths_.empty()) {
        result.status_ = IRWindow::FileDialogStatus::FAILED;
    }
}

inline void bindFileDialog(LuaScript &script) {
    sol::state &lua = script.lua();
    sol::object existing = lua["IRDialog"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRDialog must be a table before bindLuaFileDialog()");
    }
    sol::table dialog =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    sol::table mode = lua.create_table();
#define IR_BIND_DIALOG_MODE(name)                                                                  \
    mode[#name] = static_cast<lua_Integer>(IRWindow::FileDialogMode::name)
    IR_BIND_DIALOG_MODE(OPEN_FILE);
    IR_BIND_DIALOG_MODE(OPEN_FILES);
    IR_BIND_DIALOG_MODE(OPEN_FOLDER);
#undef IR_BIND_DIALOG_MODE
    dialog["Mode"] = mode;

    sol::table status = lua.create_table();
#define IR_BIND_DIALOG_STATUS(name)                                                                \
    status[#name] = static_cast<lua_Integer>(IRWindow::FileDialogStatus::name)
    IR_BIND_DIALOG_STATUS(ACCEPTED);
    IR_BIND_DIALOG_STATUS(CANCELLED);
    IR_BIND_DIALOG_STATUS(REFUSED);
    IR_BIND_DIALOG_STATUS(UNSUPPORTED);
    IR_BIND_DIALOG_STATUS(FAILED);
#undef IR_BIND_DIALOG_STATUS
    dialog["Status"] = status;

    // The script outlives this closure: its sol::state is the first member
    // LuaScript destroys.
    LuaScript *owner = &script;
    dialog["open"] = [owner](sol::object argument, sol::this_state state) {
        const IRWindow::FileDialogRequest request = fileDialogRequestFromLua(argument);
        IRWindow::FileDialogResult result;
        if (const LuaScript::FileDialogInvoker &invoker = owner->fileDialogInvoker()) {
            result = invoker(request);
        } else {
            result.status_ = IRWindow::FileDialogStatus::UNSUPPORTED;
            result.error_ = "this script has no file dialog host";
        }
        grantFileDialogPaths(owner->pathGrants(), request.mode_, result);

        sol::state_view view{state};
        sol::table out = view.create_table(0, 3);
        out["status"] = static_cast<lua_Integer>(result.status_);
        sol::table paths = view.create_table(static_cast<int>(result.paths_.size()), 0);
        for (std::size_t i = 0; i < result.paths_.size(); ++i) {
            paths[i + 1] = result.paths_[i];
        }
        out["paths"] = paths;
        if (!result.error_.empty()) {
            out["error"] = result.error_;
        }
        return out;
    };

    lua["IRDialog"] = dialog;
}

} // namespace IRScript::detail

#endif // LUA_FILE_DIALOG_BINDINGS_H
