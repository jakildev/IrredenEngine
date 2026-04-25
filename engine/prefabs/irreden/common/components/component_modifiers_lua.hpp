#ifndef COMPONENT_MODIFIERS_LUA_H
#define COMPONENT_MODIFIERS_LUA_H

// Reflection-only Lua bindings for the modifier framework. Exposes
// the value types and the TransformKind enum; the `ir.modifier.*`
// behavior API ships separately. See docs/design/modifiers.md.

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::Modifier> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::LambdaModifier> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::ResolvedField> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Modifiers> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_GlobalModifiers> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_NoGlobalModifiers> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LambdaModifiers> = true;
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ResolvedFields> = true;

template <> inline void bindLuaType<IRComponents::Modifier>(LuaScript &luaScript) {
    using IRComponents::Modifier;
    using IRComponents::TransformKind;

    luaScript.registerEnum<TransformKind>(
        "TransformKind",
        {
            {"ADD",       TransformKind::ADD},
            {"MULTIPLY",  TransformKind::MULTIPLY},
            {"SET",       TransformKind::SET},
            {"CLAMP_MIN", TransformKind::CLAMP_MIN},
            {"CLAMP_MAX", TransformKind::CLAMP_MAX},
            {"OVERRIDE",  TransformKind::OVERRIDE},
        }
    );

    luaScript.registerType<Modifier, Modifier()>(
        "Modifier",
        "field",          &Modifier::field_,
        "kind",            &Modifier::kind_,
        "param",           &Modifier::param_,
        "source",          &Modifier::source_,
        "ticksRemaining",  &Modifier::ticksRemaining_
    );
}

template <> inline void bindLuaType<IRComponents::LambdaModifier>(LuaScript &luaScript) {
    using IRComponents::LambdaModifier;

    // fn_ deliberately not exposed — lambda push goes through the
    // dedicated API surface in child 4 (`ir.modifier.pushLambda`).
    luaScript.registerType<LambdaModifier, LambdaModifier()>(
        "LambdaModifier",
        "field",           &LambdaModifier::field_,
        "source",          &LambdaModifier::source_,
        "ticksRemaining",  &LambdaModifier::ticksRemaining_
    );
}

template <> inline void bindLuaType<IRComponents::ResolvedField>(LuaScript &luaScript) {
    using IRComponents::ResolvedField;
    luaScript.registerType<ResolvedField, ResolvedField()>(
        "ResolvedField",
        "field", &ResolvedField::field_,
        "value", &ResolvedField::value_
    );
}

template <> inline void bindLuaType<IRComponents::C_Modifiers>(LuaScript &luaScript) {
    using IRComponents::C_Modifiers;
    luaScript.registerType<C_Modifiers, C_Modifiers()>(
        "C_Modifiers",
        "modifiers", &C_Modifiers::modifiers_
    );
}

template <> inline void bindLuaType<IRComponents::C_GlobalModifiers>(LuaScript &luaScript) {
    using IRComponents::C_GlobalModifiers;
    luaScript.registerType<C_GlobalModifiers, C_GlobalModifiers()>(
        "C_GlobalModifiers",
        "modifiers", &C_GlobalModifiers::modifiers_
    );
}

template <> inline void bindLuaType<IRComponents::C_NoGlobalModifiers>(LuaScript &luaScript) {
    using IRComponents::C_NoGlobalModifiers;
    luaScript.registerType<C_NoGlobalModifiers, C_NoGlobalModifiers()>(
        "C_NoGlobalModifiers"
    );
}

template <> inline void bindLuaType<IRComponents::C_LambdaModifiers>(LuaScript &luaScript) {
    using IRComponents::C_LambdaModifiers;
    luaScript.registerType<C_LambdaModifiers, C_LambdaModifiers()>(
        "C_LambdaModifiers",
        "modifiers", &C_LambdaModifiers::modifiers_
    );
}

template <> inline void bindLuaType<IRComponents::C_ResolvedFields>(LuaScript &luaScript) {
    using IRComponents::C_ResolvedFields;
    luaScript.registerType<C_ResolvedFields, C_ResolvedFields()>(
        "C_ResolvedFields",
        "fields", &C_ResolvedFields::fields_
    );
}

} // namespace IRScript

#endif /* COMPONENT_MODIFIERS_LUA_H */
