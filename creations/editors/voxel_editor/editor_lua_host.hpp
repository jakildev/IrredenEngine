#ifndef IR_VOXEL_EDITOR_LUA_HOST_H
#define IR_VOXEL_EDITOR_LUA_HOST_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/script/ir_script_utils.hpp>

#include "recipes_panel.hpp"
#include "lua_session_bindings.hpp"
#include "module_session_view.hpp"
#include "palette.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Creation-module host: `--module <dir>` runs `<dir>/init.lua` in the Lua VM
// the editor's world owns. A module registers components
// (`IRComponent.register`), recipes and panels through the `IREditor` table:
//
//   IREditor.registerRecipe(name, params, fn, opts?)
//     params: list of { name, type = IREditor.ParamType.INT | FLOAT,
//                       default, min, max, step? }
//       step: FLOAT only, finite and > 0. The param's values are the lattice
//         min + k * step clamped to [min, max]: its slider snaps there on
//         every drag and nudge, APPLY and handle drags evaluate there, and
//         Session::Builder applyRecipe may target it. default (min when
//         omitted) must be a lattice value exactly; a step on an INT, a bad
//         step, or an off-lattice default is a load error.
//     opts.classes: up to 16 unique class names, mapped to palette slots
//     fn(values, size, mask) -> { {x, y, z, color?}, ... }
//       values: param name -> value (INT params arrive as integers)
//       size:   { x, y, z } of the target voxel set
//       mask:   list of {x, y, z, class}; mask.at(x, y, z) returns class or nil
//       cells are set-local; color is a Color or {r, g, b, a?}, and a cell
//       without one takes the active palette colour.
//     opts.handles: list of { name, params, at, drag } control points, drawn on
//       a part this recipe was APPLYed to while transform mode is on:
//       name   unique within the recipe
//       params non-empty list of the recipe's param names the handle writes
//       at(values, size) -> {x, y, z}: the handle's set-local point, which
//         turns and scales with the part; anything but three finite numbers,
//         or a raise, is logged and the handle is not drawn
//       drag(values, size, point) -> { <bound param> = number, ... }
//         point: { x, y, z }, the whole set-local cell the cursor is dragged
//         to on the screen plane through `at`; values are the ones the drag
//         started from. INT results are rounded, stepped FLOAT results
//         snapped to their lattice, and every result is clamped to its
//         param's range. A key outside `params`, a non-number, or a
//         raise is logged and that drag frame changes nothing.
//     Each drag frame that changes the values replaces the cells the recipe
//     made at the old values with the cells it makes at the new ones, and the
//     release commits the drag as one undo record. An unknown opts key or a
//     malformed handle is a load error.
//   IREditor.registerPanel(name, buildFn)
//     buildFn(x, y, w, h) builds IRGui widgets inside the panel the editor
//     docks at (x, y) with size (w, h).
//   IREditor.registerSession(name, fn)
//     fn(session) compiles the module's gestures and expectations through the
//     same Session::Builder used by built-in authoring sessions.
//
// A recipe, and its handles' `at` and `drag`, are pure functions of their
// arguments, so the editor may evaluate them any number of times (the
// module_loaded session predicts the cells it will write that way).
//
// The host keeps no sol references: the recipe, handle and panel functions
// live in the hidden `IREditor._recipeFns` / `_handleFns` / `_panelFns` /
// `_sessionFns` tables and are fetched per call.
// The World closes the lua_State before process-exit static destruction, so a
// sol reference held by a static host would unref against a dead state.
namespace IRVoxelEditor {

class ModuleHost {
  public:
    // Installs the IREditor table. Runs from the editor's binding callback,
    // before IREngine::init hands the callbacks to the world.
    void bind(IRScript::LuaScript &script) {
        m_script = &script;
        sol::state &lua = script.lua();
        sol::table editor = lua.create_named_table("IREditor");
        editor["_recipeFns"] = lua.create_table();
        editor["_handleFns"] = lua.create_table();
        editor["_panelFns"] = lua.create_table();
        editor["_sessionFns"] = lua.create_table();
        LuaSession::bind(script, editor);

        sol::table paramType = lua.create_table();
#define IR_BIND_RECIPE_PARAM_TYPE(name)                                                            \
    paramType[#name] = static_cast<lua_Integer>(RecipeParamType::name)
        IR_BIND_RECIPE_PARAM_TYPE(INT);
        IR_BIND_RECIPE_PARAM_TYPE(FLOAT);
#undef IR_BIND_RECIPE_PARAM_TYPE
        editor["ParamType"] = paramType;

        editor["registerRecipe"] = [this](
                                       const std::string &name,
                                       sol::table params,
                                       sol::protected_function fn,
                                       sol::object opts
                                   ) { registerRecipe(name, params, std::move(fn), opts); };
        editor["registerPanel"] = [this](const std::string &name, sol::protected_function build) {
            registerPanel(name, std::move(build));
        };
        editor["registerSession"] = [this](const std::string &name, sol::protected_function fn) {
            registerSession(name, std::move(fn));
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
        const std::vector<RecipeClassMaskEntry> &mask,
        std::vector<RecipeCell> &out,
        std::string &error
    ) const {
        out.clear();
        const ModuleRecipe &recipe = m_recipes[recipeIndex];
        sol::state &lua = m_script->lua();
        sol::table maskTable = lua.create_table();
        for (std::size_t i = 0; i < mask.size(); ++i) {
            const RecipeClassMaskEntry &entry = mask[i];
            maskTable[static_cast<lua_Integer>(i + 1)] = lua.create_table_with(
                1,
                entry.local_.x,
                2,
                entry.local_.y,
                3,
                entry.local_.z,
                4,
                entry.className_
            );
        }
        maskTable.set_function("at", [&mask](int x, int y, int z) -> sol::optional<std::string> {
            const auto found = std::find_if(mask.begin(), mask.end(), [&](const auto &entry) {
                return entry.local_ == IRMath::ivec3(x, y, z);
            });
            if (found == mask.end())
                return sol::nullopt;
            return found->className_;
        });
        const sol::protected_function fn =
            lua["IREditor"]["_recipeFns"][static_cast<lua_Integer>(recipeIndex + 1)];
        sol::protected_function_result result =
            fn(valueTable(recipe, values), ivec3Table(setSize), maskTable);
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

    // Calls handle @p handleIndex's `at`. Returns false with @p error set when
    // it raises or returns something other than three finite numbers.
    bool evaluateHandleAt(
        std::size_t recipeIndex,
        std::size_t handleIndex,
        const std::vector<float> &values,
        IRMath::ivec3 setSize,
        IRMath::vec3 &out,
        std::string &error
    ) const {
        const ModuleRecipe &recipe = m_recipes[recipeIndex];
        const std::string where =
            "recipe '" + recipe.name_ + "' handle '" + recipe.handles_[handleIndex].name_ + "'";
        sol::protected_function_result result = handleFn(recipeIndex, handleIndex, "at")(
            valueTable(recipe, values),
            ivec3Table(setSize)
        );
        if (!result.valid()) {
            const sol::error err = result;
            error = where + " at raised: " + err.what();
            return false;
        }
        const sol::object returned = result;
        if (returned.get_type() == sol::type::table) {
            const sol::table point = returned.as<sol::table>();
            const auto component = [&point](const char *key, int index) {
                const sol::object keyed = point[key];
                return keyed.get_type() == sol::type::number ? keyed : sol::object(point[index]);
            };
            const sol::optional<float> x = component("x", 1).as<sol::optional<float>>();
            const sol::optional<float> y = component("y", 2).as<sol::optional<float>>();
            const sol::optional<float> z = component("z", 3).as<sol::optional<float>>();
            if (x && y && z && IRMath::isFinite(*x) && IRMath::isFinite(*y) &&
                IRMath::isFinite(*z)) {
                out = IRMath::vec3(*x, *y, *z);
                return true;
            }
        }
        error = where + " at must return {x, y, z} finite numbers";
        return false;
    }

    // Calls handle @p handleIndex's `drag` at set-local cell @p point and
    // writes @p values, with the handle's params replaced by what it returned,
    // to @p out: INT params rounded, stepped FLOAT params snapped to their
    // lattice, every param clamped to its range. Returns
    // false with @p error set, and @p out untouched, when `drag` raises,
    // returns a non-table, or returns a key the handle does not bind or a
    // value that is not a number.
    bool evaluateHandleDrag(
        std::size_t recipeIndex,
        std::size_t handleIndex,
        const std::vector<float> &values,
        IRMath::ivec3 setSize,
        IRMath::ivec3 point,
        std::vector<float> &out,
        std::string &error
    ) const {
        const ModuleRecipe &recipe = m_recipes[recipeIndex];
        const RecipeHandle &handle = recipe.handles_[handleIndex];
        const std::string where = "recipe '" + recipe.name_ + "' handle '" + handle.name_ + "'";
        sol::protected_function_result result = handleFn(recipeIndex, handleIndex, "drag")(
            valueTable(recipe, values),
            ivec3Table(setSize),
            ivec3Table(point)
        );
        if (!result.valid()) {
            const sol::error err = result;
            error = where + " drag raised: " + err.what();
            return false;
        }
        const sol::object returned = result;
        if (returned.get_type() != sol::type::table) {
            error = where + " drag must return a table of param values";
            return false;
        }
        std::vector<float> dragged = values;
        for (const auto &[key, value] : returned.as<sol::table>()) {
            const auto bound =
                std::find_if(handle.params_.begin(), handle.params_.end(), [&](std::size_t param) {
                    return key.get_type() == sol::type::string &&
                           recipe.params_[param].name_ == key.as<std::string>();
                });
            if (bound == handle.params_.end()) {
                error = where + " drag returned a key it does not bind";
                return false;
            }
            const RecipeParam &param = recipe.params_[*bound];
            const sol::optional<float> number = value.as<sol::optional<float>>();
            if (value.get_type() != sol::type::number || !number || !IRMath::isFinite(*number)) {
                error = where + " drag returned a non-number for '" + param.name_ + "'";
                return false;
            }
            dragged[*bound] = param.type_ == RecipeParamType::INT
                                  ? IRMath::clamp(
                                        static_cast<float>(IRMath::roundHalfUp(*number)),
                                        IRMath::ceil(param.min_),
                                        IRMath::floor(param.max_)
                                    )
                                  : recipeParamLatticeValue(
                                        param,
                                        IRMath::clamp(*number, param.min_, param.max_)
                                    );
        }
        out = std::move(dragged);
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

    const std::vector<std::string> &sessionNames() const {
        return m_sessionNames;
    }

    Session::Recipe
    buildSession(const std::string &name, IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) const {
        const auto found = std::find(m_sessionNames.begin(), m_sessionNames.end(), name);
        const ModuleSessionView view = sessionView();
        Session::Builder builder(name, sceneSize, sceneOrigin, &view);
        if (found == m_sessionNames.end()) {
            builder.recordError("module session '" + name + "' is not registered");
            return builder.finish();
        }
        const std::size_t index = static_cast<std::size_t>(found - m_sessionNames.begin());
        const std::shared_ptr<LuaSession::Lifetime> lifetime =
            std::make_shared<LuaSession::Lifetime>(LuaSession::Lifetime{&builder, m_script});
        struct Invalidate {
            std::shared_ptr<LuaSession::Lifetime> lifetime_;
            ~Invalidate() {
                lifetime_->builder_ = nullptr;
            }
        } invalidate{lifetime};
        const sol::protected_function fn =
            m_script->lua()["IREditor"]["_sessionFns"][static_cast<lua_Integer>(index + 1)];
        const sol::protected_function_result result = fn(LuaSession::Handle{lifetime});
        if (!result.valid()) {
            const sol::error error = result;
            builder.recordError("session '" + name + "' raised: " + error.what());
        }
        Session::Recipe recipe = builder.finish();
        bool hasWork = false;
        for (const Session::Segment &segment : recipe.segments_)
            hasWork = hasWork || !segment.events_.empty() || !segment.assertions_.empty();
        if (!hasWork && recipe.errors_.empty())
            recipe.errors_.push_back("session '" + name + "' records no gesture or expectation");
        return recipe;
    }

    std::size_t componentCount() const {
        return m_script->luaTypedComponents().size();
    }

    IRScript::LuaScript &script() const {
        return *m_script;
    }

    ModuleSessionView sessionView() const {
        ModuleSessionView view;
        view.recipes_ = m_recipes;
        for (const IRScript::LuaTypedComponentInfo &info : m_script->luaTypedComponents())
            view.components_.push_back(makeComponentRecord(*m_script, info.name_));
        const std::size_t moduleCount = view.components_.size();
        for (std::string name : IRPrefab::Prefab::listComponentFactories()) {
            const auto duplicate = std::find_if(
                view.components_.begin(),
                view.components_.begin() + static_cast<std::ptrdiff_t>(moduleCount),
                [&name](const ComponentRecord &component) { return component.name_ == name; }
            );
            if (duplicate == view.components_.begin() + static_cast<std::ptrdiff_t>(moduleCount))
                view.components_.push_back(makeComponentRecord(*m_script, std::move(name)));
        }
        view.evaluate_ = [this](
                             std::size_t recipe,
                             const std::vector<float> &values,
                             IRMath::ivec3 size,
                             const std::vector<RecipeClassMaskEntry> &mask,
                             std::vector<RecipeCell> &cells,
                             std::string &error
                         ) { return evaluate(recipe, values, size, mask, cells, error); };
        view.handleAt_ = [this](
                             std::size_t recipe,
                             std::size_t handle,
                             const std::vector<float> &values,
                             IRMath::ivec3 size,
                             IRMath::vec3 &point,
                             std::string &error
                         ) { return evaluateHandleAt(recipe, handle, values, size, point, error); };
        view.handleDrag_ = [this](
                               std::size_t recipe,
                               std::size_t handle,
                               const std::vector<float> &values,
                               IRMath::ivec3 size,
                               IRMath::ivec3 point,
                               std::vector<float> &out,
                               std::string &error
                           ) {
            return evaluateHandleDrag(recipe, handle, values, size, point, out, error);
        };
        return view;
    }

  private:
    void registerRecipe(
        const std::string &name,
        sol::table params,
        sol::protected_function fn,
        const sol::object &opts
    ) {
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
                "IREditor.registerRecipe('" + name + "'): declares " + std::to_string(count) +
                " params; at most " + std::to_string(kMaxRecipeParams) + " allowed"
            };
        }
        for (std::size_t i = 1; i <= count; ++i)
            recipe.params_.push_back(parseParam(name, params[i]));
        sol::state &lua = m_script->lua();
        sol::table handleFns = lua.create_table();
        if (opts.get_type() != sol::type::none && opts.get_type() != sol::type::lua_nil) {
            const std::string where = "IREditor.registerRecipe('" + name + "')";
            if (opts.get_type() != sol::type::table)
                throw sol::error{where + ": opts must be a table"};
            for (const auto &[key, value] : opts.as<sol::table>()) {
                const std::string keyName =
                    key.get_type() == sol::type::string ? key.as<std::string>() : std::string();
                if (keyName == "classes")
                    parseClasses(where, recipe, value);
                else if (keyName == "handles")
                    parseHandles(where, recipe, value, handleFns);
                else
                    throw sol::error{where + ": unknown opts key (known: classes, handles)"};
            }
        }
        m_recipes.push_back(std::move(recipe));
        const lua_Integer index = static_cast<lua_Integer>(m_recipes.size());
        lua["IREditor"]["_recipeFns"][index] = std::move(fn);
        lua["IREditor"]["_handleFns"][index] = handleFns;
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

    void registerSession(const std::string &name, sol::protected_function fn) {
        if (!LuaSession::validName(name)) {
            throw sol::error{"IREditor.registerSession: '" + name + "' must match [a-z][a-z0-9_]*"};
        }
        if (std::find(m_sessionNames.begin(), m_sessionNames.end(), name) != m_sessionNames.end())
            throw sol::error{"IREditor.registerSession: '" + name + "' is already registered"};
        if (!fn.valid())
            throw sol::error{"IREditor.registerSession('" + name + "'): fn must be a function"};
        m_sessionNames.push_back(name);
        sol::table fns = m_script->lua()["IREditor"]["_sessionFns"];
        fns[static_cast<lua_Integer>(m_sessionNames.size())] = std::move(fn);
    }

    static RecipeParam parseParam(const std::string &recipeName, const sol::object &entry) {
        const std::string where = "IREditor.registerRecipe('" + recipeName + "')";
        if (entry.get_type() != sol::type::table)
            throw sol::error{
                where + ": each param must be a { name, type, default, min, max, step? } table"
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
        const sol::object stepObj = t["step"];
        if (stepObj.get_type() != sol::type::lua_nil && stepObj.get_type() != sol::type::none) {
            if (param.type_ != RecipeParamType::FLOAT)
                throw sol::error{field + ": step is for FLOAT params only"};
            const sol::optional<float> step = stepObj.as<sol::optional<float>>();
            if (stepObj.get_type() != sol::type::number || !step || !IRMath::isFinite(*step) ||
                !(*step > 0.0f))
                throw sol::error{field + ": step must be a finite number > 0"};
            param.step_ = *step;
            const float lattice = recipeParamLatticeValue(param, param.default_);
            if (lattice != param.default_) {
                throw sol::error{
                    field + ": default " + recipeValueText(param.default_) +
                    " is not on its step lattice (nearest " + recipeValueText(lattice) + ")"
                };
            }
        }
        return param;
    }

    // Fills @p recipe.classes_ from an opts.classes list.
    static void
    parseClasses(const std::string &where, ModuleRecipe &recipe, const sol::object &list) {
        if (list.get_type() != sol::type::table)
            throw sol::error{where + ": classes must be a list"};
        const sol::table classes = list.as<sol::table>();
        if (classes.size() > static_cast<std::size_t>(kPaletteCount))
            throw sol::error{where + ": at most " + std::to_string(kPaletteCount) + " classes"};
        for (std::size_t i = 1; i <= classes.size(); ++i) {
            const sol::optional<std::string> className = classes[i];
            if (!className || !LuaSession::validName(*className))
                throw sol::error{where + ": class names must match [a-z][a-z0-9_]*"};
            if (std::find(recipe.classes_.begin(), recipe.classes_.end(), *className) !=
                recipe.classes_.end())
                throw sol::error{where + ": duplicate class '" + *className + "'"};
            recipe.classes_.push_back(*className);
        }
    }

    // Fills @p recipe.handles_ and @p fns (handle index + 1 -> { at, drag })
    // from an opts.handles list.
    void parseHandles(
        const std::string &where, ModuleRecipe &recipe, const sol::object &list, sol::table &fns
    ) const {
        if (list.get_type() != sol::type::table)
            throw sol::error{where + ": opts.handles must be a list"};
        const sol::table handles = list.as<sol::table>();
        for (std::size_t i = 1; i <= handles.size(); ++i) {
            const sol::object entry = handles[i];
            if (entry.get_type() != sol::type::table)
                throw sol::error{
                    where + ": each handle must be a { name, params, at, drag } table"
                };
            const sol::table t = entry.as<sol::table>();
            const sol::object nameObj = t["name"];
            if (nameObj.get_type() != sol::type::string || nameObj.as<std::string>().empty())
                throw sol::error{where + ": a handle is missing its name"};
            RecipeHandle handle;
            handle.name_ = nameObj.as<std::string>();
            const std::string field = where + " handle '" + handle.name_ + "'";
            for (const RecipeHandle &existing : recipe.handles_) {
                if (existing.name_ == handle.name_)
                    throw sol::error{field + ": name is already used by this recipe"};
            }
            const sol::object paramsObj = t["params"];
            if (paramsObj.get_type() != sol::type::table || paramsObj.as<sol::table>().size() == 0)
                throw sol::error{field + ": params must be a non-empty list of param names"};
            const sol::table names = paramsObj.as<sol::table>();
            for (std::size_t p = 1; p <= names.size(); ++p) {
                const sol::object paramName = names[p];
                const auto declared = std::find_if(
                    recipe.params_.begin(),
                    recipe.params_.end(),
                    [&paramName](const RecipeParam &param) {
                        return paramName.get_type() == sol::type::string &&
                               param.name_ == paramName.as<std::string>();
                    }
                );
                if (declared == recipe.params_.end())
                    throw sol::error{field + ": params names a param the recipe does not declare"};
                const std::size_t index =
                    static_cast<std::size_t>(declared - recipe.params_.begin());
                if (std::find(handle.params_.begin(), handle.params_.end(), index) !=
                    handle.params_.end())
                    throw sol::error{field + ": params names '" + declared->name_ + "' twice"};
                handle.params_.push_back(index);
            }
            const sol::object at = t["at"];
            const sol::object drag = t["drag"];
            if (at.get_type() != sol::type::function || drag.get_type() != sol::type::function)
                throw sol::error{field + ": at and drag must be functions"};
            recipe.handles_.push_back(std::move(handle));
            fns[static_cast<lua_Integer>(recipe.handles_.size())] =
                m_script->lua().create_table_with("at", at, "drag", drag);
        }
    }

    sol::protected_function
    handleFn(std::size_t recipeIndex, std::size_t handleIndex, const char *which) const {
        return m_script->lua()["IREditor"]["_handleFns"][static_cast<lua_Integer>(recipeIndex + 1)]
                              [static_cast<lua_Integer>(handleIndex + 1)][which];
    }

    // @p values as the param name -> value table a recipe function receives.
    sol::table valueTable(const ModuleRecipe &recipe, const std::vector<float> &values) const {
        sol::table table = m_script->lua().create_table();
        for (std::size_t i = 0; i < recipe.params_.size(); ++i) {
            const RecipeParam &param = recipe.params_[i];
            if (param.type_ == RecipeParamType::INT)
                table[param.name_] = static_cast<lua_Integer>(IRMath::roundHalfUp(values[i]));
            else
                table[param.name_] = values[i];
        }
        return table;
    }

    sol::table ivec3Table(IRMath::ivec3 value) const {
        return m_script->lua().create_table_with("x", value.x, "y", value.y, "z", value.z);
    }

    IRScript::LuaScript *m_script = nullptr;
    std::string m_dir;
    std::vector<ModuleRecipe> m_recipes;
    std::vector<std::string> m_panelNames;
    std::vector<std::string> m_sessionNames;
};

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_LUA_HOST_H */
