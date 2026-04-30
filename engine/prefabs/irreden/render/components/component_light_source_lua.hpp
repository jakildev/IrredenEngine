#ifndef COMPONENT_LIGHT_SOURCE_LUA_H
#define COMPONENT_LIGHT_SOURCE_LUA_H

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LightSource> = true;

template <> inline void bindLuaType<IRComponents::C_LightSource>(LuaScript &luaScript) {
    using IRComponents::C_LightSource;
    using IRComponents::LightType;

    luaScript.registerEnum<LightType>(
        "LightType",
        {{"DIRECTIONAL", LightType::DIRECTIONAL},
         {"POINT", LightType::POINT},
         {"EMISSIVE", LightType::EMISSIVE},
         {"SPOT", LightType::SPOT}}
    );

    luaScript.registerType<
        C_LightSource,
        C_LightSource(LightType, IRMath::Color, float, uint8_t, IRMath::vec3, float, float),
        C_LightSource(LightType, IRMath::Color, float, uint8_t, IRMath::vec3, float),
        C_LightSource(LightType, IRMath::Color, float, uint8_t, IRMath::vec3),
        C_LightSource(LightType, IRMath::Color, float, uint8_t),
        C_LightSource()>(
        "C_LightSource",
        "type",
        &C_LightSource::type_,
        "emitColor",
        &C_LightSource::emitColor_,
        "intensity",
        &C_LightSource::intensity_,
        "radius",
        &C_LightSource::radius_,
        "direction",
        &C_LightSource::direction_,
        "coneAngleDeg",
        &C_LightSource::coneAngleDeg_,
        "ambient",
        &C_LightSource::ambient_
    );
}

} // namespace IRScript

#endif /* COMPONENT_LIGHT_SOURCE_LUA_H */
