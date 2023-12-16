#ifndef COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H
#define COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>

using namespace IRRender;

namespace IRComponents {

    struct C_FrameDataTrixelToFramebuffer {
        FrameDataTrixelToFramebuffer frameData_;
        std::pair<ReourceId, Buffer*> buffer_;

        C_FrameDataTrixelToFramebuffer()
        :   frameData_{}
        ,   buffer_ = IRRender::createResource<Buffer>(
                nullptr,
                sizeof(FrameDataTrixelToFramebuffer),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataUniformIsoTriangles
            )
        {

        }

        void onDestroy() {
            IRRender::destroyResource<Buffer>(
                buffer_.first
            );
        }
    };

} // namespace IRComponents

#endif /* COMPONENT_FRAME_DATA_TRIXEL_TO_FRAMEBUFFER_H */
