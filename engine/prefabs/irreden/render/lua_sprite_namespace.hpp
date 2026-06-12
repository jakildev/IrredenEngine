#ifndef LUA_SPRITE_NAMESPACE_H
#define LUA_SPRITE_NAMESPACE_H

// Exposes the ir.sprite.* Lua namespace for sprite creation and animation
// control. Call bindSpriteNamespace(luaScript) from a creation's
// lua_bindings.cpp after the sol2 state is open.
//
// Entity IDs cross the Lua/C++ boundary as raw integers (IREntity::EntityId,
// which is uint64). Creation scripts receive entity IDs from the returned
// values of ir.sprite.create and ir.sprite.loadSheet.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/entities/entity_sprite_sheet.hpp>
#include <irreden/render/sprite_animation.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>

namespace IRScript {

inline void bindSpriteNamespace(LuaScript &luaScript) {
    using namespace IRComponents;
    using IREntity::EntityId;

    auto &lua = luaScript.lua();
    if (!lua["ir"].valid()) {
        lua["ir"] = lua.create_table();
    }

    auto spriteTbl = lua.create_table();

    spriteTbl["ONCE"] = static_cast<int>(SpriteLoopMode::ONCE);
    spriteTbl["LOOP"] = static_cast<int>(SpriteLoopMode::LOOP);
    spriteTbl["PING_PONG"] = static_cast<int>(SpriteLoopMode::PING_PONG);

    // ir.sprite.loadSheet(name, path) → entityId
    // Loads name.png + name.irsprite from path and creates an entity
    // that owns the resulting C_SpriteSheet. The entity lives until
    // destroyed explicitly; scripts should hold the returned id.
    spriteTbl["loadSheet"] = [](const std::string &name, const std::string &path) -> EntityId {
        auto sheet = IRSprite::loadSpriteSheet(name, path);
        return IREntity::createEntity(std::move(sheet));
    };

    // ir.sprite.create(sheetEntityId, x, y, z, screenPixelSmooth?) → entityId
    // Creates a sprite entity with C_LocalTransform, C_Sprite, and
    // C_SpriteAnimation. The initial C_Sprite size is derived from the
    // first frame of the sheet. Call ir.sprite.playAnimation afterward
    // to start playback. `screenPixelSmooth` defaults to false (sprite
    // snaps to the game-pixel grid); pass true for the player avatar or
    // a camera-locked entity that should move between game pixels.
    spriteTbl["create"] = [](EntityId sheetEntity,
                             float x,
                             float y,
                             float z,
                             sol::optional<bool> screenPixelSmoothOpt) -> EntityId {
        auto sheetOpt = IREntity::getComponentOptional<C_SpriteSheet>(sheetEntity);
        IRMath::vec2 frameSize{16.0f, 16.0f};
        IRRender::ResourceId texHandle = 0;
        if (sheetOpt.has_value()) {
            const auto *s = sheetOpt.value();
            texHandle = s->textureHandle_;
            if (!s->frames_.empty()) {
                frameSize = IRMath::vec2{
                    static_cast<float>(s->frames_[0].sizePx_.x),
                    static_cast<float>(s->frames_[0].sizePx_.y)
                };
            }
        } else {
            IRE_LOG_WARN(
                "ir.sprite.create: entity {} has no C_SpriteSheet — sprite will be invisible",
                sheetEntity
            );
        }
        C_Sprite sprite{texHandle, frameSize};
        sprite.screenPixelSmooth_ = screenPixelSmoothOpt.value_or(false);
        return IREntity::createEntity(
            IRComponents::C_LocalTransform{IRMath::vec3{x, y, z}},
            std::move(sprite),
            C_SpriteAnimation{}
        );
    };

    // ir.sprite.playAnimation(spriteEnt, sheetEnt, animName, loopMode?, speed?)
    spriteTbl["playAnimation"] = [](EntityId spriteEnt,
                                    EntityId sheetEnt,
                                    const std::string &animName,
                                    sol::optional<int> loopModeOpt,
                                    sol::optional<float> speedOpt) {
        const auto loopMode = loopModeOpt.has_value()
                                  ? static_cast<SpriteLoopMode>(loopModeOpt.value())
                                  : SpriteLoopMode::LOOP;
        const float speed = speedOpt.value_or(1.0f);
        IRPrefab::Sprite::playAnimation(spriteEnt, sheetEnt, animName, loopMode, speed);
    };

    spriteTbl["stopAnimation"] = [](EntityId spriteEnt) {
        IRPrefab::Sprite::stopAnimation(spriteEnt);
    };

    spriteTbl["getCurrentFrame"] = [](EntityId spriteEnt) -> int {
        return IRPrefab::Sprite::getCurrentFrame(spriteEnt);
    };

    spriteTbl["getCurrentAnimation"] = [](EntityId spriteEnt) -> std::string {
        return IRPrefab::Sprite::getCurrentAnimation(spriteEnt);
    };

    lua["ir"]["sprite"] = spriteTbl;
}

} // namespace IRScript

#endif /* LUA_SPRITE_NAMESPACE_H */
