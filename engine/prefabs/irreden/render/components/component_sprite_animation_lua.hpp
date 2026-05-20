#ifndef COMPONENT_SPRITE_ANIMATION_LUA_H
#define COMPONENT_SPRITE_ANIMATION_LUA_H

#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_SpriteAnimation> = true;

template <> inline void bindLuaType<IRComponents::C_SpriteAnimation>(LuaScript &luaScript) {
    using IRComponents::C_SpriteAnimation;
    using IRComponents::SpriteLoopMode;

    luaScript.registerType<C_SpriteAnimation, C_SpriteAnimation()>(
        "C_SpriteAnimation",
        "frameIndex",     &C_SpriteAnimation::frameIndex_,
        "loopMode",       &C_SpriteAnimation::loopMode_,
        "speed",          &C_SpriteAnimation::speed_,
        "terminated",     &C_SpriteAnimation::terminated_,
        "stopped",        &C_SpriteAnimation::stopped_,
        "currentAnimName",&C_SpriteAnimation::currentAnimName_
    );

    luaScript.registerEnum<SpriteLoopMode>(
        "SpriteLoopMode",
        {{"ONCE",      SpriteLoopMode::ONCE},
         {"LOOP",      SpriteLoopMode::LOOP},
         {"PING_PONG", SpriteLoopMode::PING_PONG}}
    );
}

} // namespace IRScript

#endif /* COMPONENT_SPRITE_ANIMATION_LUA_H */
