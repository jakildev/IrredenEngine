#ifndef ENTITY_FRAMEBUFFER_H
#define ENTITY_FRAMEBUFFER_H

#include <irreden/common/components/component_name.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>

using namespace IRComponents;

namespace IREntity {

template <> struct Prefab<PrefabTypes::kFramebuffer> {
    static EntityId create(
        std::string framebufferName,
        ivec2 framebufferSize,
        ivec2 framebufferExtraPixelBufferSize,
        float startZoomLevel = 1.0f
    ) {
        // C_LocalTransform / C_WorldTransform auto-attach in createEntity;
        // FRAMEBUFFER_TO_SCREEN reads C_WorldTransform.translation_ for the
        // camera-offset math (default identity translation == vec3(0)).
        EntityId framebufer = createEntity(
            C_Name{framebufferName},
            C_TrixelCanvasFramebuffer{framebufferSize, framebufferExtraPixelBufferSize},
            C_FrameDataTrixelToFramebuffer{},
            C_ZoomLevel{startZoomLevel}
        );

        setName(framebufer, framebufferName);
        return framebufer;
    }
};
} // namespace IREntity

#endif /* ENTITY_FRAMEBUFFER_H */
