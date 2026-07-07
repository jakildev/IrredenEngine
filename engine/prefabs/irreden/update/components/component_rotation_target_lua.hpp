#ifndef COMPONENT_ROTATION_TARGET_LUA_H
#define COMPONENT_ROTATION_TARGET_LUA_H

#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_RotationTarget> = true;

template <> inline void bindLuaType<IRComponents::C_RotationTarget>(LuaScript &luaScript) {
    using IRComponents::C_RotationTarget;
    // Scalar fields are bound as read-write member pointers (the C_MidiNote /
    // C_ZoomLevel shape), so a Lua automation lane can drive `input_` in place
    // — `arch.C_RotationTarget:at(i).input = cc` writes straight through the
    // column (`:at` hands Lua a std::ref to the row). The axis + easing curve
    // are construction-time config, set through the constructors below; `axis_`
    // (a vec3) stays constructor-only — it's fixed config, not a per-frame Lua
    // write (the C_Velocity3D precedent). A math-typed field that DOES need
    // per-frame Lua writes uses the `sol::property` {x,y,z[,w]}-table convention
    // now on C_LocalTransform (rotation/translation/scale) — see
    // engine/script/CLAUDE.md "C++-component per-field writes from Lua".
    luaScript.registerType<
        C_RotationTarget,
        C_RotationTarget(),
        C_RotationTarget(IRMath::vec3, float, float),
        C_RotationTarget(IRMath::vec3, float, float, float)>(
        "C_RotationTarget",
        "input",
        &C_RotationTarget::input_,
        "minAngle",
        &C_RotationTarget::minAngle_,
        "maxAngle",
        &C_RotationTarget::maxAngle_,
        "inputMin",
        &C_RotationTarget::inputMin_,
        "inputMax",
        &C_RotationTarget::inputMax_
    );
}
} // namespace IRScript

#endif /* COMPONENT_ROTATION_TARGET_LUA_H */
