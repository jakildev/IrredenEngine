#ifndef COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H
#define COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>

using namespace IRRender;

namespace IRComponents {

struct C_FrameDataTrixelToFramebuffer {
    FrameDataTrixelToFramebuffer frameData_;

    C_FrameDataTrixelToFramebuffer()
        : frameData_{} {}

    void updateFrameData(const Buffer *frameDataBuffer) const {
        frameDataBuffer->subData(0, sizeof(FrameDataTrixelToFramebuffer), &frameData_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H */
