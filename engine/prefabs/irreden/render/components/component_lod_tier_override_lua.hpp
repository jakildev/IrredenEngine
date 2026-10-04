#ifndef COMPONENT_LOD_TIER_OVERRIDE_LUA_H
#define COMPONENT_LOD_TIER_OVERRIDE_LUA_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_lod_tier_override.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>

#include <optional>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LodTierOverride> = true;

namespace detail {

// `tier` is an `IRRender.LodLevel` integer; absent keeps the default tier.
inline IRComponents::C_LodTierOverride
lodTierOverrideFromLua(const sol::optional<sol::table> &fields) {
    if (!fields) {
        return IRComponents::C_LodTierOverride{};
    }
    const sol::optional<lua_Integer> tier = (*fields)["tier"];
    if (!tier) {
        return IRComponents::C_LodTierOverride{};
    }
    const std::optional<IRRender::LodLevel> level = IRRender::lodLevelFromIndex(*tier);
    if (!level) {
        throw sol::error{"C_LodTierOverride: tier must be an IRRender.LodLevel value (0..4)"};
    }
    return IRComponents::C_LodTierOverride{*level};
}

} // namespace detail

// Attaches from Lua through `IREntity.addLuaComponent(e, IRComponent.C_LodTierOverride,
// { tier = IRRender.LodLevel.LOD_0 })` and from a prefab's declarative
// `components = { C_LodTierOverride = { tier = ... } }` block; detaches through
// `IREntity.removeLuaComponent`. Both attach paths change the entity's
// archetype, so a tick body defers them.
template <> inline void bindLuaType<IRComponents::C_LodTierOverride>(LuaScript &luaScript) {
    using IRComponents::C_LodTierOverride;
    luaScript.registerType<
        C_LodTierOverride,
        C_LodTierOverride(IRRender::LodLevel),
        C_LodTierOverride()>("C_LodTierOverride", "tier", &C_LodTierOverride::tier_);

    luaScript.registerComponentAttachFactory(
        IREntity::getEntityManager().getComponentType<C_LodTierOverride>(),
        [](IREntity::EntityId entity, const sol::optional<sol::table> &fields) {
            IREntity::setComponent(entity, detail::lodTierOverrideFromLua(fields));
        }
    );
    IRPrefab::Prefab::registerComponentFactory(
        "C_LodTierOverride",
        [](IREntity::EntityId entity, const sol::table &fields) {
            IREntity::setComponent(entity, detail::lodTierOverrideFromLua(fields));
        }
    );
}

} // namespace IRScript

#endif /* COMPONENT_LOD_TIER_OVERRIDE_LUA_H */
