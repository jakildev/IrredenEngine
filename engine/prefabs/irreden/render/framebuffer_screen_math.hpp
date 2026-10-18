#ifndef FRAMEBUFFER_SCREEN_MATH_H
#define FRAMEBUFFER_SCREEN_MATH_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

// Placement of a framebuffer's colour on the default screen target. Every pass
// that draws a framebuffer as the screen base (`FRAMEBUFFER_TO_SCREEN`, and
// `FRAMEBUFFER_GLITCH` in its stead) computes its MVP here, so the two cannot
// place the same framebuffer differently.
namespace IRSystem::detail {

inline IRMath::mat4 framebufferScreenProjection() {
    return IRMath::ortho(
        0.0f,
        static_cast<float>(IRRender::getViewport().x),
        0.0f,
        static_cast<float>(IRRender::getViewport().y),
        -1.0f,
        100.0f
    );
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
