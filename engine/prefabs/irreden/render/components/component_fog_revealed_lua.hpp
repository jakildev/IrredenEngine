#ifndef COMPONENT_FOG_REVEALED_LUA_H
#define COMPONENT_FOG_REVEALED_LUA_H

#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_FogRevealed> = true;

template <> inline void bindLuaType<IRComponents::C_FogRevealed>(LuaScript &luaScript) {
    using IRComponents::C_FogRevealed;
    using IRComponents::FogHiddenPolicy;
    using IRComponents::FogOverride;
    sol::usertype<C_FogRevealed> type = luaScript.registerType<C_FogRevealed, C_FogRevealed()>(
        "C_FogRevealed",
        "revealFactor",
        &C_FogRevealed::revealFactor_,
        "shown",
        &C_FogRevealed::shown_,
        "channels",
        &C_FogRevealed::channels_
    );
    type["override"] = sol::property(
        [](const C_FogRevealed &value) { return static_cast<lua_Integer>(value.override_); },
        [](C_FogRevealed &value, sol::object overrideValue) {
            if (!overrideValue.is<lua_Integer>()) {
                throw sol::error{"C_FogRevealed.override must be an IRComponent.FogOverride value"};
            }
            const lua_Integer raw = overrideValue.as<lua_Integer>();
            switch (raw) {
            case static_cast<lua_Integer>(FogOverride::NONE):
            case static_cast<lua_Integer>(FogOverride::FORCE_HIDDEN):
            case static_cast<lua_Integer>(FogOverride::FORCE_REVEALED):
                value.override_ = static_cast<FogOverride>(raw);
                return;
            default:
                throw sol::error{"C_FogRevealed.override must be an IRComponent.FogOverride value"};
            }
        }
    );
    type["hiddenPolicy"] = sol::property(
        [](const C_FogRevealed &value) { return static_cast<lua_Integer>(value.hiddenPolicy_); },
        [](C_FogRevealed &value, sol::object policyValue) {
            if (!policyValue.is<lua_Integer>()) {
                throw sol::error{
                    "C_FogRevealed.hiddenPolicy must be an IRComponent.FogHiddenPolicy value"
                };
            }
            const lua_Integer raw = policyValue.as<lua_Integer>();
            switch (raw) {
            case static_cast<lua_Integer>(FogHiddenPolicy::HIDE):
            case static_cast<lua_Integer>(FogHiddenPolicy::GHOST):
                value.hiddenPolicy_ = static_cast<FogHiddenPolicy>(raw);
                return;
            default:
                throw sol::error{
                    "C_FogRevealed.hiddenPolicy must be an IRComponent.FogHiddenPolicy value"
                };
            }
        }
    );
    type["ghostHeld"] = sol::readonly(&C_FogRevealed::ghostHeld_);

    sol::table overrides = luaScript.lua().create_table();
#define IR_BIND_FOG_OVERRIDE(name) overrides[#name] = static_cast<lua_Integer>(FogOverride::name)
    IR_BIND_FOG_OVERRIDE(NONE);
    IR_BIND_FOG_OVERRIDE(FORCE_HIDDEN);
    IR_BIND_FOG_OVERRIDE(FORCE_REVEALED);
#undef IR_BIND_FOG_OVERRIDE
    luaScript.lua()["IRComponent"]["FogOverride"] = overrides;

    sol::table policies = luaScript.lua().create_table();
#define IR_BIND_FOG_HIDDEN_POLICY(name)                                                            \
    policies[#name] = static_cast<lua_Integer>(FogHiddenPolicy::name)
    IR_BIND_FOG_HIDDEN_POLICY(HIDE);
    IR_BIND_FOG_HIDDEN_POLICY(GHOST);
#undef IR_BIND_FOG_HIDDEN_POLICY
    luaScript.lua()["IRComponent"]["FogHiddenPolicy"] = policies;
}

} // namespace IRScript

#endif /* COMPONENT_FOG_REVEALED_LUA_H */
