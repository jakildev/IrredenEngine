#ifndef LOD_ZOOM_POLICY_H
#define LOD_ZOOM_POLICY_H

// Creation-side configuration of the zoom -> LOD tier mapping for the main
// camera (the entity named "camera") and each secondary viewport, and reads of
// the tiers they resolved. The setters change the camera entity's archetype
// when it has no profile yet, so call them on the main thread outside archetype
// iteration. Policy: docs/design/lod-strategy.md §"Phase 1d".

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_lod_zoom_profile.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/lod_zoom_profile.hpp>

#include <string>

namespace IRPrefab::Lod {

namespace detail {

// kNullEntity when no entity carries the name (a headless world).
inline IREntity::EntityId mainCamera() {
    const std::string name{"camera"};
    const IREntity::EntityManager &entityManager = IREntity::getEntityManager();
    return entityManager.hasName(name) ? entityManager.getEntityByName(name)
                                       : IREntity::kNullEntity;
}

template <typename Component> inline Component *componentOrNull(IREntity::EntityId entity) {
    if (entity == IREntity::kNullEntity || !IREntity::entityExists(entity)) {
        return nullptr;
    }
    auto found = IREntity::getComponentOptional<Component>(entity);
    return found.has_value() ? *found : nullptr;
}

inline bool setProfileOn(
    IREntity::EntityId entity,
    const IRRender::LodZoomProfile::Thresholds &thresholds,
    float hysteresisOctaves
) {
    if (entity == IREntity::kNullEntity || !IREntity::entityExists(entity)) {
        return false;
    }
    if (auto *profile = componentOrNull<IRComponents::C_LodZoomProfile>(entity)) {
        return profile->set(thresholds, hysteresisOctaves);
    }
    IRComponents::C_LodZoomProfile added{};
    if (!added.set(thresholds, hysteresisOctaves)) {
        return false;
    }
    IREntity::setComponent(entity, added);
    return true;
}

inline void clearProfileOn(IREntity::EntityId entity) {
    if (componentOrNull<IRComponents::C_LodZoomProfile>(entity) != nullptr) {
        IREntity::removeComponent<IRComponents::C_LodZoomProfile>(entity);
    }
}

} // namespace detail

// The main camera's profile: its C_LodZoomProfile, else the default.
inline IRRender::LodZoomProfile mainZoomProfile() {
    const auto *profile =
        detail::componentOrNull<IRComponents::C_LodZoomProfile>(detail::mainCamera());
    return profile != nullptr ? profile->profile_ : IRRender::LodZoomProfile{};
}

// The profile @p viewport selects with: its own C_LodZoomProfile, else the
// main camera's.
inline IRRender::LodZoomProfile viewportZoomProfile(IREntity::EntityId viewport) {
    const auto *profile = detail::componentOrNull<IRComponents::C_LodZoomProfile>(viewport);
    return profile != nullptr ? profile->profile_ : mainZoomProfile();
}

// Each setter returns false, leaving the previous profile in place, when the
// values break a rule `IRRender::LodZoomProfile::whyInvalid` names or the
// target does not exist.
inline bool setMainZoomProfile(
    const IRRender::LodZoomProfile::Thresholds &thresholds, float hysteresisOctaves = 0.0f
) {
    return detail::setProfileOn(detail::mainCamera(), thresholds, hysteresisOctaves);
}

inline bool setViewportZoomProfile(
    IREntity::EntityId viewport,
    const IRRender::LodZoomProfile::Thresholds &thresholds,
    float hysteresisOctaves = 0.0f
) {
    if (detail::componentOrNull<IRComponents::C_ViewportCamera>(viewport) == nullptr) {
        return false;
    }
    return detail::setProfileOn(viewport, thresholds, hysteresisOctaves);
}

// Back to the default profile.
inline void clearMainZoomProfile() {
    detail::clearProfileOn(detail::mainCamera());
}

// Back to inheriting the main camera's profile.
inline void clearViewportZoomProfile(IREntity::EntityId viewport) {
    if (detail::componentOrNull<IRComponents::C_ViewportCamera>(viewport) != nullptr) {
        detail::clearProfileOn(viewport);
    }
}

// The next LOD_UPDATE takes `tierAt(zoom)` instead of moving from the
// previous tier, as at creation.
inline void resetMainZoomHistory() {
    IREntity::singleton<IRComponents::C_ActiveLodLevel>().history_.reset();
}

inline void resetViewportZoomHistory(IREntity::EntityId viewport) {
    if (auto *camera = detail::componentOrNull<IRComponents::C_ViewportCamera>(viewport)) {
        camera->lodHistory_.reset();
    }
}

// The tier @p viewport's subjects were banded at by the last
// SYNC_VIEWPORT_SUBJECTS; LOD_4 before the first one.
inline IRRender::LodLevel viewportTier(IREntity::EntityId viewport) {
    const auto *camera = detail::componentOrNull<IRComponents::C_ViewportCamera>(viewport);
    return camera != nullptr ? camera->lodHistory_.tier_ : IRRender::LodLevel::LOD_4;
}

} // namespace IRPrefab::Lod

#endif /* LOD_ZOOM_POLICY_H */
