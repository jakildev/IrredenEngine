#ifndef FRAMEBUFFER_SCREEN_MATH_H
#define FRAMEBUFFER_SCREEN_MATH_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

// Placement of a framebuffer's colour on the default screen target, and of a
// framebuffer over its own twin. `FRAMEBUFFER_TO_SCREEN` alone places on
// screen; `FRAMEBUFFER_GLITCH` draws into an image the blit then places, so no
// two passes can place the same framebuffer differently.
namespace IRSystem::detail {

// Orthographic projection over a target of @p size, in its own pixels with
// the origin at its lower-left corner.
inline IRMath::mat4 targetProjection(IRMath::ivec2 size) {
    return IRMath::ortho(
        0.0f,
        static_cast<float>(size.x),
        0.0f,
        static_cast<float>(size.y),
        -1.0f,
        100.0f
    );
}

inline IRMath::mat4 framebufferScreenProjection() {
    return targetProjection(IRRender::getViewport());
}

// @p carriesCameraResidual is true only for the main framebuffer: it alone
// carries the screen-pixel half of the camera's anti-vibration split
// (`IRPrefab::Camera::screenResidual`), whose framebuffer half
// `TRIXEL_TO_FRAMEBUFFER` consumes, so the two cannot disagree at game-pixel
// boundaries.
inline IRMath::mat4 framebufferScreenModel(
    IRMath::ivec2 resolutionPlusBuffer,
    IRMath::vec3 cameraPosition,
    IRMath::vec2 cameraScreenResidual,
    bool carriesCameraResidual
) {
    const IRMath::ivec2 scaleFactor = IRRender::getOutputScaleFactor();
    const IRMath::vec2 screenCenter = IRMath::vec2(IRRender::getViewport()) / 2.0f;
    IRMath::vec2 offset = screenCenter + IRMath::isoDeltaToScreenDelta(
                                             IRMath::pos3DtoPos2DIso(cameraPosition),
                                             IRRender::getTriangleStepSizeScreen()
                                         );
    if (carriesCameraResidual) {
        offset += cameraScreenResidual;
    }
    IRMath::mat4 model =
        IRMath::translate(IRMath::mat4(1.0f), IRMath::vec3(offset.x, offset.y, 0.0f));
    return IRMath::scale(
        model,
        IRMath::vec3(
            resolutionPlusBuffer.x * scaleFactor.x,
            resolutionPlusBuffer.y * scaleFactor.y,
            1.0f
        )
    );
}

// Placement of a framebuffer-sized quad over a target of the same size, texel
// for texel: a pass that rewrites a framebuffer's colour into a twin image
// (`FRAMEBUFFER_GLITCH`) draws with this, and the ordinary blit then places
// the twin on screen as it would the framebuffer itself. The quad sits at the
// screen MVP's depth so a target's depth test passes it as the blit's does.
inline IRMath::mat4 framebufferSelfMvp(IRMath::ivec2 resolutionPlusBuffer) {
    const IRMath::vec2 size = IRMath::vec2(resolutionPlusBuffer);
    const IRMath::mat4 model = IRMath::scale(
        IRMath::translate(IRMath::mat4(1.0f), IRMath::vec3(size * 0.5f, 0.0f)),
        IRMath::vec3(size, 1.0f)
    );
    return targetProjection(resolutionPlusBuffer) * model;
}

inline IRMath::mat4 framebufferScreenMvp(
    IRMath::ivec2 resolutionPlusBuffer,
    IRMath::vec3 cameraPosition,
    IRMath::vec2 cameraScreenResidual,
    bool carriesCameraResidual
) {
    return framebufferScreenProjection() * framebufferScreenModel(
                                               resolutionPlusBuffer,
                                               cameraPosition,
                                               cameraScreenResidual,
                                               carriesCameraResidual
                                           );
}

} // namespace IRSystem::detail

#endif /* FRAMEBUFFER_SCREEN_MATH_H */
