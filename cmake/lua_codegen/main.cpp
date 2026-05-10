// Build-time tool: scans Lua schema files for `IRComponent.register(...)`
// calls and emits a C++ header containing component structs + Lua bindings
// + a registration helper. The CODEGEN side of the Lua-driven ECS epic
// (see docs/design/lua-driven-ecs.md). Runs the input as Lua against a
// stub `IRComponent` table whose `register` callback captures every call;
// non-component surfaces (`IRSystem`, `IRTime`, etc.) are no-op stubs so
// schema files that share a creation's `main.lua` still load cleanly.
//
// Usage: ir_lua_codegen --out <output.hpp> <input1.lua> [input2.lua ...]
//
// Field types supported in CODEGEN mode: int32, float, bool, string.
// Tables and functions in component schemas are an explicit codegen-time
// error pointing at file/line/field — those fields belong in EVAL mode.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

enum class FieldType { INT32, FLOAT, BOOL, STRING };

struct Field {
    std::string name_;
    FieldType type_;
    std::variant<std::int32_t, float, bool, std::string> default_;
};

struct Component {
    std::string name_;
    std::string sourceFile_;
    std::vector<Field> fields_;
};

struct Capture {
    std::vector<Component> components_;
    std::string currentSource_;
};

// Caller is the Lua `IRComponent.register` shim. Inspects the schema table
// and either appends a Component to capture or raises a Lua error with a
// schema-pointing message (file/component/field).
[[noreturn]] void schemaError(sol::this_state ts, const std::string &message) {
    luaL_error(ts, "%s", message.c_str());
    std::abort(); // unreachable; appeases [[noreturn]]
}

const char *fieldTypeName(FieldType t) {
    switch (t) {
        case FieldType::INT32: return "int32";
        case FieldType::FLOAT: return "float";
        case FieldType::BOOL: return "bool";
        case FieldType::STRING: return "string";
    }
    return "?";
}

const char *fieldCppType(FieldType t) {
    switch (t) {
        case FieldType::INT32: return "std::int32_t";
        case FieldType::FLOAT: return "float";
        case FieldType::BOOL: return "bool";
        case FieldType::STRING: return "std::string";
    }
    return "?";
}

std::string escapeStringLiteral(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string renderDefaultLiteral(const Field &f) {
    switch (f.type_) {
        case FieldType::INT32: return std::to_string(std::get<std::int32_t>(f.default_));
        case FieldType::FLOAT: {
            // Always emit a decimal point so the literal is a valid C++
            // float (`100f` is a parse error; `100.f` is fine).
            const float v = std::get<float>(f.default_);
            std::ostringstream os;
            os.precision(9);
            os << v;
            std::string s = os.str();
            if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
                s.find('E') == std::string::npos) {
                s += ".0";
            }
            s += "f";
            return s;
        }
        case FieldType::BOOL: return std::get<bool>(f.default_) ? "true" : "false";
        case FieldType::STRING: return escapeStringLiteral(std::get<std::string>(f.default_));
    }
    return "/*unknown*/";
}

// Map a Lua value to a (FieldType, default) pair via the same inference
// rules as IComponentDataLuaTyped: integer literals → int32, float
// literals → float, string → std::string, bool → bool. Anything else
// triggers a clear schema error pointing at component/field.
Field inferFromShortValue(
    sol::this_state ts,
    const std::string &componentName,
    const std::string &fieldName,
    const sol::object &value
) {
    Field f;
    f.name_ = fieldName;
    if (value.is<sol::nil_t>()) {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName + "' has nil default"
        );
    }
    if (value.is<bool>() && value.get_type() == sol::type::boolean) {
        f.type_ = FieldType::BOOL;
        f.default_ = value.as<bool>();
        return f;
    }
    if (value.get_type() == sol::type::number) {
        // Lua 5.4 distinguishes integer/float subtypes; sol2's typed get
        // collapses them. Push the value back onto the stack so we can
        // call lua_isinteger, which is the authoritative subtype check.
        // (A C++-side round-trip can't distinguish `0` from `0.0` because
        // both round-trip cleanly through int64.)
        lua_State *L = ts;
        sol::stack::push(L, value);
        const bool isInt = lua_isinteger(L, -1) != 0;
        lua_pop(L, 1);
        if (isInt) {
            const std::int64_t asInt = value.as<std::int64_t>();
            if (asInt < std::numeric_limits<std::int32_t>::min() ||
                asInt > std::numeric_limits<std::int32_t>::max()) {
                schemaError(
                    ts,
                    "lua_codegen: field '" + componentName + "." + fieldName +
                        "' integer default is out of int32 range"
                );
            }
            f.type_ = FieldType::INT32;
            f.default_ = static_cast<std::int32_t>(asInt);
        } else {
            f.type_ = FieldType::FLOAT;
            f.default_ = static_cast<float>(value.as<double>());
        }
        return f;
    }
    if (value.get_type() == sol::type::string) {
        f.type_ = FieldType::STRING;
        f.default_ = value.as<std::string>();
        return f;
    }
    if (value.get_type() == sol::type::table) {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName +
                "' uses nested-table short form. Use the explicit "
                "{ type = '...', default = ... } form, or move this "
                "component to EVAL mode (table fields are EVAL-only)."
        );
    }
    if (value.get_type() == sol::type::function) {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName +
                "' is a function. Function fields are EVAL-only."
        );
    }
    schemaError(
        ts,
        "lua_codegen: field '" + componentName + "." + fieldName +
            "' has unsupported default type."
    );
}

Field inferFromExplicitTable(
    sol::this_state ts,
    const std::string &componentName,
    const std::string &fieldName,
    const sol::table &spec
) {
    Field f;
    f.name_ = fieldName;
    sol::optional<std::string> typeOpt = spec["type"];
    if (!typeOpt) {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName +
                "' uses table form but no 'type' key"
        );
    }
    const std::string typeStr = *typeOpt;
    sol::object defaultVal = spec["default"];

    if (typeStr == "int32") {
        f.type_ = FieldType::INT32;
        if (defaultVal.is<sol::nil_t>()) {
            f.default_ = std::int32_t{0};
        } else {
            f.default_ = static_cast<std::int32_t>(defaultVal.as<double>());
        }
    } else if (typeStr == "float") {
        f.type_ = FieldType::FLOAT;
        if (defaultVal.is<sol::nil_t>()) {
            f.default_ = 0.0f;
        } else {
            f.default_ = static_cast<float>(defaultVal.as<double>());
        }
    } else if (typeStr == "bool") {
        f.type_ = FieldType::BOOL;
        if (defaultVal.is<sol::nil_t>()) {
            f.default_ = false;
        } else {
            f.default_ = defaultVal.as<bool>();
        }
    } else if (typeStr == "string") {
        f.type_ = FieldType::STRING;
        if (defaultVal.is<sol::nil_t>()) {
            f.default_ = std::string{};
        } else {
            f.default_ = defaultVal.as<std::string>();
        }
    } else if (typeStr == "table" || typeStr == "function") {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName + "' has type '" +
                typeStr + "' which is EVAL-only (not supported in CODEGEN)"
        );
    } else {
        schemaError(
            ts,
            "lua_codegen: field '" + componentName + "." + fieldName + "' has unknown type '" +
                typeStr + "'. Allowed: int32, float, bool, string."
        );
    }
    return f;
}

void registerComponentCb(
    sol::this_state ts,
    Capture &cap,
    const std::string &name,
    const sol::table &schema
) {
    for (const auto &existing : cap.components_) {
        if (existing.name_ == name) {
            schemaError(
                ts,
                "lua_codegen: duplicate component '" + name +
                    "' (already registered earlier in this codegen run)"
            );
        }
    }
    Component comp;
    comp.name_ = name;
    comp.sourceFile_ = cap.currentSource_;
    schema.for_each([&](const sol::object &key, const sol::object &value) {
        if (!key.is<std::string>()) {
            schemaError(
                ts,
                "lua_codegen: component '" + name + "' has a non-string field key"
            );
        }
        const std::string fieldName = key.as<std::string>();
        if (value.get_type() == sol::type::table) {
            const sol::table tbl = value.as<sol::table>();
            sol::object typeKey = tbl["type"];
            if (typeKey.is<std::string>()) {
                comp.fields_.push_back(
                    inferFromExplicitTable(ts, name, fieldName, tbl)
                );
                return;
            }
            // Bare nested table → schema error.
            comp.fields_.push_back(inferFromShortValue(ts, name, fieldName, value));
            return;
        }
        comp.fields_.push_back(inferFromShortValue(ts, name, fieldName, value));
    });
    // Lua hash-table iteration order is implementation-defined and not
    // stable across runs. Sort fields alphabetically by name so the
    // generated struct has a deterministic field/constructor order.
    std::sort(
        comp.fields_.begin(),
        comp.fields_.end(),
        [](const Field &a, const Field &b) { return a.name_ < b.name_; }
    );
    cap.components_.push_back(std::move(comp));
}

void writeOutput(const std::string &outPath, const Capture &cap) {
    std::ostringstream os;
    os << "// AUTO-GENERATED by cmake/lua_codegen — do not edit by hand.\n";
    os << "// Generated from:\n";
    for (const auto &c : cap.components_) {
        os << "//   " << c.sourceFile_ << " (component " << c.name_ << ")\n";
    }
    os << "#pragma once\n\n";
    os << "#include <cstdint>\n";
    os << "#include <string>\n\n";
    os << "#include <irreden/script/lua_binding_traits.hpp>\n";
    os << "#include <irreden/script/lua_script.hpp>\n";
    os << "#include <irreden/ir_entity.hpp>\n\n";

    os << "namespace IRComponents {\n\n";
    for (const auto &c : cap.components_) {
        const std::string structName = "C_" + c.name_;
        os << "// From " << c.sourceFile_ << "\n";
        os << "struct " << structName << " {\n";
        for (const auto &f : c.fields_) {
            os << "    " << fieldCppType(f.type_) << " " << f.name_ << "_ = "
               << renderDefaultLiteral(f) << ";\n";
        }
        os << "\n";
        os << "    " << structName << "() = default;\n";
        // Field-by-field constructor to mirror the Lua `Comp.new(...)` shape
        // and to give the Lua binding's sol::constructors a non-default ctor.
        if (!c.fields_.empty()) {
            os << "    " << structName << "(";
            for (size_t i = 0; i < c.fields_.size(); ++i) {
                if (i) os << ", ";
                os << fieldCppType(c.fields_[i].type_) << " " << c.fields_[i].name_;
            }
            os << ")\n";
            for (size_t i = 0; i < c.fields_.size(); ++i) {
                os << (i == 0 ? "        : " : "        , ");
                os << c.fields_[i].name_ << "_{" << c.fields_[i].name_ << "}\n";
            }
            os << "    {}\n";
        }
        os << "};\n\n";
    }
    os << "} // namespace IRComponents\n\n";

    os << "namespace IRScript {\n\n";
    for (const auto &c : cap.components_) {
        const std::string structName = "C_" + c.name_;
        os << "template <> inline constexpr bool kHasLuaBinding<IRComponents::" << structName
           << "> = true;\n\n";
        os << "template <> inline void bindLuaType<IRComponents::" << structName
           << ">(LuaScript &luaScript) {\n";
        os << "    using IRComponents::" << structName << ";\n";
        os << "    luaScript.registerType<" << structName;
        if (c.fields_.empty()) {
            os << ", " << structName << "()";
        } else {
            os << ", " << structName << "(";
            for (size_t i = 0; i < c.fields_.size(); ++i) {
                if (i) os << ", ";
                os << fieldCppType(c.fields_[i].type_);
            }
            os << ")";
        }
        os << ">(\n";
        os << "        \"" << structName << "\"";
        for (const auto &f : c.fields_) {
            os << ",\n        " << escapeStringLiteral(f.name_) << ",\n";
            os << "        [](" << structName << " &obj) { return obj." << f.name_ << "_; }";
        }
        os << "\n    );\n";
        os << "}\n\n";
    }
    os << "} // namespace IRScript\n\n";

    // Registration helper: pre-registers each codegen'd component with the
    // EntityManager (so its ComponentId is allocated up front, matching the
    // EVAL path's behaviour) and binds the Lua usertype via the trait. The
    // creation calls this once during init after `bindLuaDrivenEcs()`.
    os << "namespace IRScript::CodegenRegistry {\n\n";
    os << "inline void registerCodegenComponents(IRScript::LuaScript &luaScript) {\n";
    os << "    auto &em = IREntity::getEntityManager();\n";
    for (const auto &c : cap.components_) {
        const std::string structName = "C_" + c.name_;
        os << "    em.getComponentType<IRComponents::" << structName << ">();\n";
        os << "    luaScript.registerTypeFromTraits<IRComponents::" << structName << ">();\n";
    }
    os << "}\n\n";
    os << "} // namespace IRScript::CodegenRegistry\n";

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "lua_codegen: failed to open output '" << outPath << "'\n";
        std::exit(2);
    }
    out << os.str();
}

void usage(const char *progName) {
    std::cerr << "usage: " << progName << " --out <output.hpp> <input1.lua> [input2.lua ...]\n";
}

} // namespace

int main(int argc, char **argv) {
    std::string outPath;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out") {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            outPath = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            inputs.push_back(arg);
        }
    }
    if (outPath.empty() || inputs.empty()) {
        usage(argv[0]);
        return 1;
    }

    Capture cap;
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::package, sol::lib::string,
                       sol::lib::table, sol::lib::math);

    sol::table irComponent = lua.create_named_table("IRComponent");
    irComponent.set_function(
        "register",
        [&cap](sol::this_state ts, const std::string &name, const sol::table &schema) {
            registerComponentCb(ts, cap, name, schema);
            // Return a stub handle table so user-side `local C = IRComponent.register(...)`
            // captures something callable for subsequent toplevel statements.
            sol::state_view sv(ts);
            sol::table handle = sv.create_table();
            handle["typeName"] = name;
            handle["componentId"] = 0;
            handle["fields"] = sv.create_table();
            return handle;
        }
    );

    // No-op stubs for non-component surfaces. Schema files that share a
    // creation's runtime main.lua may reference these at top-level; we
    // accept the calls and discard the args. T-107 will replace
    // IRSystem.registerSystem with a real codegen path.
    sol::table irSystem = lua.create_named_table("IRSystem");
    irSystem.set_function("registerSystem", [](sol::variadic_args) { return 0; });
    irSystem.set_function("registerPipeline", [](sol::variadic_args) {});
    irSystem.set_function("systemId", [](sol::variadic_args) { return 0; });
    irSystem.set_function("replaceSystemBody", [](sol::variadic_args) {});
    irSystem["SystemName"] = lua.create_table();

    sol::table irTime = lua.create_named_table("IRTime");
    irTime["UPDATE"] = 0;
    irTime["RENDER"] = 1;
    irTime["INPUT"] = 2;
    irTime["START"] = 3;
    irTime["END"] = 4;

    sol::table irEntity = lua.create_named_table("IREntity");
    irEntity.set_function("addLuaComponent", [](sol::variadic_args) {});
    irEntity.set_function("getLuaComponent", [](sol::variadic_args) { return sol::nil; });
    irEntity.set_function("removeLuaComponent", [](sol::variadic_args) {});
    irEntity.set_function("hasLuaComponent", [](sol::variadic_args) { return false; });
    irEntity.set_function("getLuaField", [](sol::variadic_args) { return sol::nil; });
    irEntity.set_function("setLuaField", [](sol::variadic_args) {});
    irEntity.set_function("create_some_entity", [](sol::variadic_args) { return 0; });

    sol::table irModifier = lua.create_named_table("IRModifier");
    irModifier.set_function("registerField", [](sol::variadic_args) { return 0; });
    irModifier.set_function("fieldId", [](sol::variadic_args) { return 0; });
    irModifier.set_function("fieldName", [](sol::variadic_args) { return sol::nil; });
    irModifier.set_function("add", [](sol::variadic_args) {});
    irModifier.set_function("addGlobal", [](sol::variadic_args) {});
    irModifier.set_function("addLambda", [](sol::variadic_args) {});
    irModifier.set_function("removeBySource", [](sol::variadic_args) {});
    irModifier.set_function("applyToField", [](sol::variadic_args) { return 0.0; });
    irModifier.set_function("resolved", [](sol::variadic_args) { return 0.0; });
    irModifier["Transform"] = lua.create_table();

    for (const auto &input : inputs) {
        cap.currentSource_ = input;
        sol::protected_function_result result = lua.safe_script_file(
            input, sol::script_pass_on_error
        );
        if (!result.valid()) {
            sol::error err = result;
            std::cerr << "lua_codegen: error executing '" << input << "':\n  "
                      << err.what() << "\n";
            return 1;
        }
    }

    writeOutput(outPath, cap);
    return 0;
}
