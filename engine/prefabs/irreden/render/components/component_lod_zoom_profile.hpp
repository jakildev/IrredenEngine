#ifndef COMPONENT_LOD_ZOOM_PROFILE_H
#define COMPONENT_LOD_ZOOM_PROFILE_H

#include <irreden/render/lod_zoom_profile.hpp>

#include <optional>

namespace IRComponents {

// The zoom -> tier mapping of the camera entity carrying it. On the main
// camera (the entity named "camera") it drives LOD_UPDATE; on a secondary
// viewport it overrides the main camera's profile for that viewport only. A
// camera without one selects with the default profile (2, 4, 8, 16, width 0);
// a viewport without one inherits the main camera's. Authored data only: each
// consumer's previous tier lives beside its other derived camera state.
struct C_LodZoomProfile {
    IRRender::LodZoomProfile profile_{};

    C_LodZoomProfile() = default;

    explicit C_LodZoomProfile(const IRRender::LodZoomProfile &profile)
        : profile_{profile} {}

    // Replaces the profile when the values are valid; otherwise keeps the
    // current one and returns false.
    bool set(const IRRender::LodZoomProfile::Thresholds &thresholds, float hysteresisOctaves) {
        const std::optional<IRRender::LodZoomProfile> next =
            IRRender::LodZoomProfile::make(thresholds, hysteresisOctaves);
        if (!next) {
            return false;
        }
        profile_ = *next;
        return true;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_LOD_ZOOM_PROFILE_H */
