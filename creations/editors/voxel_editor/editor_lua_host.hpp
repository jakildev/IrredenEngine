#ifndef IR_VOXEL_EDITOR_LUA_HOST_H
#define IR_VOXEL_EDITOR_LUA_HOST_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/script/ir_script_utils.hpp>

#include "recipes_panel.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Creation-module host: `--module <dir>` runs `<dir>/init.lua` in the Lua VM
// the editor's world owns. A module registers components
// (`IRComponent.register`), recipes and panels through the `IREditor` table:
//
//   IREditor.registerRecipe(name, params, fn)
//     params: list of { name, type = IREditor.ParamType.INT | FLOAT,
//                       default, min, max }
//     fn(values, size) -> { {x, y, z, color?}, ... }
//       values: param name -> value (INT params arrive as integers)
//       size:   { x, y, z } of the target voxel set
//       cells are set-local; color is a Color or {r, g, b, a?}, and a cell
//       without one takes the active palette colour.
//   IREditor.registerPanel(name, buildFn)
//     buildFn(x, y, w, h) builds IRGui widgets inside the panel the editor
//     docks at (x, y) with size (w, h).
//
// A recipe is a pure function of its arguments, so the editor may evaluate it
// any number of times (the module_loaded session predicts the cells it will
// write that way).
//
// The host keeps no sol references: the recipe and panel functions live in the
// hidden `IREditor._recipeFns` / `_panelFns` tables and are fetched per call.
// The World closes the lua_State before process-exit static destruction, so a
// sol reference held by a static host would unref against a dead state.
namespace IRVoxelEditor {

enum class RecipeParamType { INT, FLOAT };

struct RecipeParam {
    std::string name_;
    RecipeParamType type_ = RecipeParamType::INT;
    float default_ = 0.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
};

struct RecipeCell {
    IRMath::ivec3 local_ = IRMath::ivec3(0);
    std::optional<IRMath::Color> color_;
};

struct ModuleRecipe {
    std::string name_;
    std::vector<RecipeParam> params_;
};

class ModuleHost {
  public:
    // Installs the IREditor table. Runs from the editor's binding callback,
    // before IREngine::init hands the callbacks to the world.
    void bind(IRScript::LuaScript &script) {
        m_script = &script;
        sol::state &lua = script.lua();
        sol::table editor = lua.create_named_table("IREditor");
        editor["_recipeFns"] = lua.create_table();
        editor["_panelFns"] = lua.create_table();

        sol::table paramType = lua.create_table();
#define IR_BIND_RECIPE_PARAM_TYPE(name)                                                            \
    paramType[#name] = static_cast<lua_Integer>(RecipeParamType::name)
        IR_BIND_RECIPE_PARAM_TYPE(INT);
        IR_BIND_RECIPE_PARAM_TYPE(FLOAT);
#undef IR_BIND_RECIPE_PARAM_TYPE
        editor["ParamType"] = paramType;

        editor["registerRecipe"] =
            [this](const std::string &name, sol::table params, sol::protected_function fn) {
                registerRecipe(name, params, std::move(fn));
            };
        editor["registerPanel"] = [this](const std::string &name, sol::protected_function build) {
            registerPanel(name, std::move(build));
        };
    }

    // Runs `<dir>/init.lua` with `<dir>/?.lua` prepended to package.path.
    // Returns false with @p error set on a missing directory or init.lua, or
    // on any Lua error the module raises while loading — including an
    // `IRComponent.register` of a name a C++ component already binds, since
    // the codegen never consumes a module.
    bool load(const std::string &dir, std::string &error) {
        const std::filesystem::path root(dir);
        const std::filesystem::path init = root / "init.lua";
        if (!std::filesystem::is_directory(root)) {
            error = "module directory not found: " + dir;
            return false;
        }
        if (!std::filesystem::is_regular_file(init)) {
            error = "module has no init.lua: " + init.string();
            return false;
        }
        m_dir = dir;
        m_script->setCodegenCoexistence(false);
        sol::state &lua = m_script->lua();
        const std::string packagePath = lua["package"]["path"];
        lua["package"]["path"] = (root / "?.lua").string() + ";" + packagePath;
        // scriptFile logs and swallows a load error; the host needs the result
        // to fail the launch.
        sol::protected_function_result result =
            lua.safe_script_file(init.string(), sol::script_pass_on_error);
        if (!result.valid()) {
            const sol::error err = result;
            error = err.what();
            return false;
        }
        return true;
    }

    // Calls the recipe's function with @p values (one per declared param, in
    // declaration order) and the target set's size. Returns false with
    // @p error set when the call raises or returns something other than a
    // list of cells.
    bool evaluate(
        std::size_t recipeIndex,
        const std::vector<float> &values,
        IRMath::ivec3 setSize,
        std::vector<RecipeCell> &out,
        std::string &error
    ) const {
        out.clear();
        const ModuleRecipe &recipe = m_recipes[recipeIndex];
        sol::state &lua = m_script->lua();
        sol::table valueTable = lua.create_table();
        for (std::size_t i = 0; i < recipe.params_.size(); ++i) {
            const RecipeParam &param = recipe.params_[i];
            if (param.type_ == RecipeParamType::INT)
                valueTable[param.name_] = static_cast<lua_Integer>(IRMath::roundHalfUp(values[i]));
            else
                valueTable[param.name_] = values[i];
        }
        sol::table sizeTable =
            lua.create_table_with("x", setSize.x, "y", setSize.y, "z", setSize.z);
        const sol::protected_function fn =
            lua["IREditor"]["_recipeFns"][static_cast<lua_Integer>(recipeIndex + 1)];
        sol::protected_function_result result = fn(valueTable, sizeTable);
        if (!result.valid()) {
            const sol::error err = result;
            error = "recipe '" + recipe.name_ + "' raised: " + err.what();
            return false;
        }
        const sol::object returned = result;
        if (returned.get_type() != sol::type::table) {
            error = "recipe '" + recipe.name_ + "' must return a list of {x, y, z, color?} cells";
            return false;
        }
        const sol::table cells = returned.as<sol::table>();
        const std::size_t count = cells.size();
        out.reserve(count);
        for (std::size_t i = 1; i <= count; ++i) {
            const sol::object entry = cells[i];
            if (entry.get_type() != sol::type::table) {
                error = "recipe '" + recipe.name_ + "' cell " + std::to_string(i) +
                        " is not a {x, y, z, color?} table";
                return false;
            }
            const sol::table cell = entry.as<sol::table>();
            const sol::optional<int> x = cell[1];
            const sol::optional<int> y = cell[2];
            const sol::optional<int> z = cell[3];
            if (!x || !y || !z) {
                error = "recipe '" + recipe.name_ + "' cell " + std::to_string(i) +
                        " needs integer x, y, z at [1], [2], [3]";
                return false;
            }
            RecipeCell parsed;
            parsed.local_ = IRMath::ivec3(*x, *y, *z);
            const sol::object color = cell[4];
            if (color.valid() && color.get_type() != sol::type::lua_nil)
                parsed.color_ = IRScript::colorFromLua(color);
            out.push_back(parsed);
        }
        return true;
    }

    // Calls panel @p panelIndex's build function at the docked rectangle.
    bool buildPanel(
        std::size_t panelIndex, IRMath::ivec2 pos, IRMath::ivec2 size, std::string &error
    ) const {
        const sol::protected_function build =
            m_script->lua()["IREditor"]["_panelFns"][static_cast<lua_Integer>(panelIndex + 1)];
        sol::protected_function_result result = build(pos.x, pos.y, size.x, size.y);
        if (!result.valid()) {
            const sol::error err = result;
            error = "panel '" + m_panelNames[panelIndex] + "' raised: " + err.what();
            return false;
        }
        return true;
    }

    bool loaded() const {
        return !m_dir.empty();
    }

    const std::string &dir() const {
        return m_dir;
    }

    const std::vector<ModuleRecipe> &recipes() const {
        return m_recipes;
    }

    const std::vector<std::string> &panelNames() const {
        return m_panelNames;
    }

    std::size_t componentCount() const {
        return m_script->luaTypedComponents().size();
    }

    IRScript::LuaScript &script() const {
        return *m_script;
    }

  private:
    void registerRecipe(const std::string &name, sol::table params, sol::protected_function fn) {
        for (const ModuleRecipe &existing : m_recipes) {
            if (existing.name_ == name)
                throw sol::error{"IREditor.registerRecipe: '" + name + "' is already registered"};
        }
        if (!fn.valid())
            throw sol::error{"IREditor.registerRecipe('" + name + "'): fn must be a function"};
        ModuleRecipe recipe;
        recipe.name_ = name;
        const std::size_t count = params.size();
        if (count > static_cast<std::size_t>(kMaxRecipeParams)) {
            throw sol::error{
                "IREditor.registerRecipe('" + name + "'): at most " +
                std::to_string(kMaxRecipeParams) + " params"
            };
        }
        for (std::size_t i = 1; i <= count; ++i)
            recipe.params_.push_back(parseParam(name, params[i]));
        m_recipes.push_back(std::move(recipe));
        sol::table fns = m_script->lua()["IREditor"]["_recipeFns"];
        fns[static_cast<lua_Integer>(m_recipes.size())] = std::move(fn);
    }

    void registerPanel(const std::string &name, sol::protected_function build) {
        for (const std::string &existing : m_panelNames) {
            if (existing == name)
                throw sol::error{"IREditor.registerPanel: '" + name + "' is already registered"};
        }
        if (!build.valid())
            throw sol::error{"IREditor.registerPanel('" + name + "'): buildFn must be a function"};
        m_panelNames.push_back(name);
        sol::table fns = m_script->lua()["IREditor"]["_panelFns"];
        fns[static_cast<lua_Integer>(m_panelNames.size())] = std::move(build);
    }

    static RecipeParam parseParam(const std::string &recipeName, const sol::object &entry) {
        const std::string where = "IREditor.registerRecipe('" + recipeName + "')";
        if (entry.get_type() != sol::type::table)
            throw sol::error{
                where + ": each param must be a { name, type, default, min, max } table"
            };
        const sol::table t = entry.as<sol::table>();
        const sol::optional<std::string> paramName = t["name"];
        if (!paramName)
            throw sol::error{where + ": a param is missing its name"};
        const std::string field = where + " param '" + *paramName + "'";
        const sol::object typeObj = t["type"];
        if (typeObj.get_type() != sol::type::number)
            throw sol::error{field + ": type must be IREditor.ParamType.INT or FLOAT"};
        RecipeParam param;
        param.name_ = *paramName;
        switch (typeObj.as<lua_Integer>()) {
        case static_cast<lua_Integer>(RecipeParamType::INT):
            param.type_ = RecipeParamType::INT;
            break;
        case static_cast<lua_Integer>(RecipeParamType::FLOAT):
            param.type_ = RecipeParamType::FLOAT;
            break;
        default:
            throw sol::error{field + ": type must be IREditor.ParamType.INT or FLOAT"};
        }
        const sol::optional<float> minValue = t["min"];
        const sol::optional<float> maxValue = t["max"];
        if (!minValue || !maxValue || *minValue >= *maxValue)
            throw sol::error{field + ": needs numeric min < max"};
        param.min_ = *minValue;
        param.max_ = *maxValue;
        param.default_ = IRMath::clamp(t.get_or("default", *minValue), *minValue, *maxValue);
        return param;
    }

    IRScript::LuaScript *m_script = nullptr;
    std::string m_dir;
    std::vector<ModuleRecipe> m_recipes;
    std::vector<std::string> m_panelNames;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_LUA_HOST_H */
