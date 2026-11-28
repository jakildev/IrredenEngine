#ifndef COMPONENT_LOD_ZOOM_PROFILE_LUA_H
#define COMPONENT_LOD_ZOOM_PROFILE_LUA_H

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_lod_zoom_profile.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LodZoomProfile> = true;

namespace detail {

// `{ thresholds = { t1, t2, t3, t4 }, hysteresis = octaves }`; an absent key
// keeps @p base's value. Raises with the rule the values break, so no caller
// ever stores an invalid profile.
inline IRRender::LodZoomProfile
lodZoomProfileFromLua(const sol::table &fields, const IRRender::LodZoomProfile &base) {
    IRRender::LodZoomProfile::Thresholds thresholds = base.thresholds();
    float hysteresis = base.hysteresisOctaves();

    const sol::object thresholdsField = fields["thresholds"];
    if (thresholdsField.valid() && thresholdsField != sol::lua_nil) {
        if (!thresholdsField.is<sol::table>()) {
            throw sol::error{"C_LodZoomProfile: thresholds must be a table of four numbers"};
        }
        const sol::table list = thresholdsField.as<sol::table>();
        if (list.size() != IRRender::kLodZoomThresholdCount) {
            throw sol::error{"C_LodZoomProfile: thresholds must be a table of four numbers"};
        }
        for (std::size_t i = 0; i < IRRender::kLodZoomThresholdCount; ++i) {
            const sol::object value = list[i + 1];
            if (!value.is<double>()) {
                throw sol::error{"C_LodZoomProfile: thresholds must be a table of four numbers"};
            }
            thresholds[i] = static_cast<float>(value.as<double>());
        }
    }
    const sol::object hysteresisField = fields["hysteresis"];
    if (hysteresisField.valid() && hysteresisField != sol::lua_nil) {
        if (!hysteresisField.is<double>()) {
            throw sol::error{"C_LodZoomProfile: hysteresis must be a number of octaves"};
        }
        hysteresis = static_cast<float>(hysteresisField.as<double>());
    }

    if (const std::optional<std::string_view> why =
            IRRender::LodZoomProfile::whyInvalid(thresholds, hysteresis)) {
        throw sol::error{"C_LodZoomProfile: " + std::string{*why}};
    }
    return *IRRender::LodZoomProfile::make(thresholds, hysteresis);
}

inline IRComponents::C_LodZoomProfile
lodZoomProfileComponentFromLua(const sol::optional<sol::table> &fields) {
    if (!fields) {
        return IRComponents::C_LodZoomProfile{};
    }
    return IRComponents::C_LodZoomProfile{lodZoomProfileFromLua(*fields, {})};
}

} // namespace detail

// Attaches through `IREntity.addLuaComponent(camera, IRComponent.C_LodZoomProfile,
// { thresholds = { 2, 4, 8, 16 }, hysteresis = 0.25 })` and a prefab's
// declarative `components = { C_LodZoomProfile = { ... } }` block, both
// archetype changes a tick body defers. On a held component, `threshold(i)`
// (1-based) and `hysteresis` read the profile and `set{ ... }` replaces the
// keys it names. Every path raises on an invalid profile and keeps the
// previous one.
template <> inline void bindLuaType<IRComponents::C_LodZoomProfile>(LuaScript &luaScript) {
    using IRComponents::C_LodZoomProfile;
    sol::usertype<C_LodZoomProfile> type =
        luaScript.registerType<C_LodZoomProfile, C_LodZoomProfile()>("C_LodZoomProfile");
    type["hysteresis"] = sol::property([](const C_LodZoomProfile &value) {
        return value.profile_.hysteresisOctaves();
    });
    type["threshold"] = [](const C_LodZoomProfile &value, lua_Integer index) {
        if (index < 1 || index > static_cast<lua_Integer>(IRRender::kLodZoomThresholdCount)) {
            throw sol::error{"C_LodZoomProfile.threshold: index must be 1..4"};
        }
        return value.profile_.thresholds()[static_cast<std::size_t>(index - 1)];
    };
    type["set"] = [](C_LodZoomProfile &value, const sol::table &fields) {
        value.profile_ = detail::lodZoomProfileFromLua(fields, value.profile_);
    };

    luaScript.registerComponentAttachFactory(
        IREntity::getEntityManager().getComponentType<C_LodZoomProfile>(),
        [](IREntity::EntityId entity, const sol::optional<sol::table> &fields) {
            IREntity::setComponent(entity, detail::lodZoomProfileComponentFromLua(fields));
        }
    );
    IRPrefab::Prefab::registerComponentFactory(
        "C_LodZoomProfile",
        [](IREntity::EntityId entity, const sol::table &fields) {
            IREntity::setComponent(entity, detail::lodZoomProfileComponentFromLua(fields));
        }
    );
}

} // namespace IRScript

#endif /* COMPONENT_LOD_ZOOM_PROFILE_LUA_H */
