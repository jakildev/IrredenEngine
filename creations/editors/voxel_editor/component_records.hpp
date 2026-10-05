#ifndef IR_VOXEL_EDITOR_COMPONENT_RECORDS_H
#define IR_VOXEL_EDITOR_COMPONENT_RECORDS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/prefab_component_factory.hpp>

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The components the COMPONENTS panel attached to the entity-scene root or a
// part. A record is the editor's copy of what the manifest's `components`
// entry will say, applied live through the component's prefab factory, so
// the authored entity and the saved manifest go through one path.
//
// Records hold typed C++ values, never sol objects: they live in static
// editor state, and the World closes the lua_State before process-exit static
// destruction.
namespace IRVoxelEditor {

// MODULE: registered by the loaded module through IRComponent.register; its
// fields are reflected. ENGINE: a C++ component with a prefab factory; it has
// no field reflection, so its overrides are a Lua table constructor.
enum class ComponentSource { MODULE, ENGINE };

// monostate holds a function or table field, which the panel shows read-only
// and the manifest never carries.
using ComponentFieldValue = std::variant<
    std::monostate,
    std::int32_t,
    float,
    bool,
    std::string,
    IRMath::vec3,
    IRMath::ivec3,
    IRMath::vec4>;

struct ComponentField {
    std::string name_;
    IRScript::LuaFieldType type_ = IRScript::LuaFieldType::INT32;
    ComponentFieldValue value_;
};

struct ComponentRecord {
    std::string name_;
    ComponentSource source_ = ComponentSource::MODULE;
    std::vector<ComponentField> fields_;
    // ENGINE only: the field overrides as a Lua table constructor.
    std::string overrides_ = "{}";
};

namespace detail {

inline std::string formatNumber(float value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return std::string(buffer, result.ptr);
}

inline std::string luaQuoted(std::string_view text) {
    std::string quoted = "\"";
    for (char c : text) {
        switch (c) {
        case '\\':
            quoted += "\\\\";
            break;
        case '"':
            quoted += "\\\"";
            break;
        case '\n':
            quoted += "\\n";
            break;
        default:
            quoted.push_back(c);
            break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

// Splits on spaces and commas, the separators the text input can type.
inline std::vector<std::string_view> splitComponents(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find_first_of(" ,", start);
        const std::size_t stop = end == std::string_view::npos ? text.size() : end;
        if (stop > start)
            parts.push_back(text.substr(start, stop - start));
        start = stop + 1;
    }
    return parts;
}

inline std::optional<std::int32_t> parseInt(std::string_view text) {
    std::int32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

// strtof rather than from_chars: libc++ ships no floating-point from_chars.
inline std::optional<float> parseFloat(std::string_view text) {
    if (text.empty())
        return std::nullopt;
    const std::string owned(text);
    char *end = nullptr;
    const float value = std::strtof(owned.c_str(), &end);
    if (end != owned.c_str() + owned.size() || !IRMath::isFinite(value))
        return std::nullopt;
    return value;
}

template <typename Vec, typename Parse>
std::optional<ComponentFieldValue> parseVector(std::string_view text, Parse parse) {
    const std::vector<std::string_view> parts = splitComponents(text);
    if (parts.size() != static_cast<std::size_t>(Vec::length()))
        return std::nullopt;
    Vec value{};
    for (int i = 0; i < Vec::length(); ++i) {
        const auto component = parse(parts[static_cast<std::size_t>(i)]);
        if (!component)
            return std::nullopt;
        value[i] = *component;
    }
    return ComponentFieldValue{value};
}

template <typename Vec> std::string formatVector(const Vec &value, const char *separator) {
    static constexpr const char *kAxes[] = {"x", "y", "z", "w"};
    std::string text;
    for (int i = 0; i < Vec::length(); ++i) {
        if (*separator == '=') {
            text += (i == 0 ? "{ " : ", ");
            text += kAxes[i];
            text += " = ";
        } else if (i > 0) {
            text += separator;
        }
        if constexpr (std::is_same_v<typename Vec::value_type, float>)
            text += formatNumber(value[i]);
        else
            text += std::to_string(value[i]);
    }
    if (*separator == '=')
        text += " }";
    return text;
}

inline ComponentFieldValue
fieldValueFromRow(IRScript::LuaFieldType type, const sol::object &value) {
    using IRScript::LuaFieldType;
    const bool isTable = value.get_type() == sol::type::table;
    switch (type) {
    case LuaFieldType::INT32:
        return value.is<int>() ? ComponentFieldValue{value.as<std::int32_t>()}
                               : ComponentFieldValue{};
    case LuaFieldType::FLOAT:
        return value.is<float>() ? ComponentFieldValue{value.as<float>()} : ComponentFieldValue{};
    case LuaFieldType::BOOL:
        return value.is<bool>() ? ComponentFieldValue{value.as<bool>()} : ComponentFieldValue{};
    case LuaFieldType::STRING:
        return value.is<std::string>() ? ComponentFieldValue{value.as<std::string>()}
                                       : ComponentFieldValue{};
    case LuaFieldType::VEC3:
        return isTable ? ComponentFieldValue{IRScript::vec3FromLua(value)} : ComponentFieldValue{};
    case LuaFieldType::IVEC3:
        return isTable ? ComponentFieldValue{IRScript::ivec3FromLua(value)} : ComponentFieldValue{};
    case LuaFieldType::VEC4:
        return isTable ? ComponentFieldValue{IRScript::vec4FromLua(value)} : ComponentFieldValue{};
    case LuaFieldType::FUNCTION:
    case LuaFieldType::TABLE:
        break;
    }
    return ComponentFieldValue{};
}

inline const IRScript::LuaTypedComponentInfo *
findModuleComponent(const IRScript::LuaScript &script, std::string_view name) {
    for (const IRScript::LuaTypedComponentInfo &info : script.luaTypedComponents()) {
        if (info.name_ == name)
            return &info;
    }
    return nullptr;
}

} // namespace detail

// What a field's text input shows: vectors as space-separated components.
inline std::string formatFieldValue(const ComponentFieldValue &value) {
    return std::visit(
        [](const auto &v) -> std::string {
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<V, std::int32_t>)
                return std::to_string(v);
            else if constexpr (std::is_same_v<V, float>)
                return detail::formatNumber(v);
            else if constexpr (std::is_same_v<V, bool>)
                return v ? "true" : "false";
            else if constexpr (std::is_same_v<V, std::string>)
                return v;
            else
                return detail::formatVector(v, " ");
        },
        value
    );
}

// Parses what the author typed into a field of @p type. Nullopt for text the
// type cannot hold; BOOL, FUNCTION and TABLE take no text.
inline std::optional<ComponentFieldValue>
parseFieldText(IRScript::LuaFieldType type, std::string_view text) {
    using IRScript::LuaFieldType;
    switch (type) {
    case LuaFieldType::INT32:
        if (auto value = detail::parseInt(text))
            return ComponentFieldValue{*value};
        return std::nullopt;
    case LuaFieldType::FLOAT:
        if (auto value = detail::parseFloat(text))
            return ComponentFieldValue{*value};
        return std::nullopt;
    case LuaFieldType::STRING:
        return ComponentFieldValue{std::string(text)};
    case LuaFieldType::VEC3:
        return detail::parseVector<IRMath::vec3>(text, detail::parseFloat);
    case LuaFieldType::IVEC3:
        return detail::parseVector<IRMath::ivec3>(text, detail::parseInt);
    case LuaFieldType::VEC4:
        return detail::parseVector<IRMath::vec4>(text, detail::parseFloat);
    case LuaFieldType::BOOL:
    case LuaFieldType::FUNCTION:
    case LuaFieldType::TABLE:
        break;
    }
    return std::nullopt;
}

// The record's manifest `components` value: the reflected fields that hold
// data, or an ENGINE record's typed overrides.
inline std::string componentLiteral(const ComponentRecord &record) {
    if (record.source_ == ComponentSource::ENGINE)
        return record.overrides_;
    std::string literal;
    for (const ComponentField &field : record.fields_) {
        const std::string value = std::visit(
            [](const auto &v) -> std::string {
                using V = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<V, std::monostate>)
                    return {};
                else if constexpr (std::is_same_v<V, std::string>)
                    return detail::luaQuoted(v);
                else if constexpr (std::is_same_v<V, bool> || std::is_arithmetic_v<V>)
                    return formatFieldValue(v);
                else
                    return detail::formatVector(v, "=");
            },
            field.value_
        );
        if (value.empty())
            continue;
        literal += (literal.empty() ? "{ " : ", ") + field.name_ + " = " + value;
    }
    return literal.empty() ? "{}" : literal + " }";
}

// A fresh record for @p name: MODULE when the loaded module registered it,
// otherwise ENGINE. Field values are filled by the first apply.
inline ComponentRecord makeComponentRecord(const IRScript::LuaScript &script, std::string name) {
    ComponentRecord record;
    if (const auto *info = detail::findModuleComponent(script, name)) {
        record.source_ = ComponentSource::MODULE;
        for (const IRScript::LuaTypedComponentField &field : info->fields_)
            record.fields_.push_back(ComponentField{field.name_, field.type_, {}});
    } else {
        record.source_ = ComponentSource::ENGINE;
    }
    record.name_ = std::move(name);
    return record;
}

// Attaches @p literal to @p entity through the component's prefab factory.
// On success the record takes the applied values: a MODULE record re-reads
// every field from the entity's row, an ENGINE record keeps @p literal. On
// failure the entity and the record are unchanged.
inline std::optional<std::string> applyComponentLiteral(
    IRScript::LuaScript &script,
    IREntity::EntityId entity,
    ComponentRecord &record,
    const std::string &literal
) {
    const IRPrefab::Prefab::ComponentFactory *factory =
        IRPrefab::Prefab::findComponentFactory(record.name_);
    if (factory == nullptr)
        return "no prefab factory is registered for '" + record.name_ + "'";
    sol::protected_function_result evaluated =
        script.lua().safe_script("return " + literal, sol::script_pass_on_error);
    if (!evaluated.valid()) {
        const sol::error error = evaluated;
        return "'" + literal + "' is not Lua: " + error.what();
    }
    const sol::object result = evaluated;
    if (result.get_type() != sol::type::table)
        return "'" + literal + "' is not a table";
    const sol::table fields = result.as<sol::table>();
    if (auto error = IRPrefab::Prefab::validateComponentFields(record.name_, fields))
        return error;
    try {
        (*factory)(entity, fields);
    } catch (const std::exception &error) {
        return "'" + record.name_ + "' factory failed: " + error.what();
    }
    if (record.source_ == ComponentSource::ENGINE) {
        record.overrides_ = literal;
        return std::nullopt;
    }
    const auto *info = detail::findModuleComponent(script, record.name_);
    const sol::object row =
        info ? script.readLuaTypedComponent(entity, info->componentId_) : sol::object{};
    if (row.get_type() != sol::type::table)
        return "'" + record.name_ + "' did not attach";
    const sol::table values = row.as<sol::table>();
    for (ComponentField &field : record.fields_)
        field.value_ = detail::fieldValueFromRow(field.type_, values[field.name_]);
    return std::nullopt;
}

inline std::optional<std::string> applyComponentRecord(
    IRScript::LuaScript &script, IREntity::EntityId entity, ComponentRecord &record
) {
    return applyComponentLiteral(script, entity, record, componentLiteral(record));
}

// Removes the record's component from @p entity, when the process can name
// its component id.
inline void detachComponentRecord(
    const IRScript::LuaScript &script, IREntity::EntityId entity, const ComponentRecord &record
) {
    IREntity::ComponentId componentId = IREntity::kNullComponent;
    if (const auto *info = detail::findModuleComponent(script, record.name_))
        componentId = info->componentId_;
    else
        componentId = script.componentIdByLuaName(record.name_);
    IREntity::EntityManager &entityManager = IREntity::getEntityManager();
    if (componentId != IREntity::kNullComponent && entityManager.hasComponent(entity, componentId))
        entityManager.removeComponentDynamic(entity, componentId);
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_COMPONENT_RECORDS_H */
