#ifndef CANVAS_SHAPE_GEOMETRY_H
#define CANVAS_SHAPE_GEOMETRY_H

#include <irreden/ir_render.hpp>

#include <span>
#include <utility>

namespace IRComponents {

// Descriptor indices are canvas-local and remain valid until its next shape upload.
// The projection snapshot describes that upload, not the currently bound global UBO.
struct CanvasShapeGeometry {
    std::pair<IRRender::ResourceId, IRRender::Buffer *> descriptors_{0, nullptr};
    std::size_t capacity_ = 0;
    IRRender::GPUShapesFrameData frameData_{};

    void reset() {
        frameData_ = {};
    }

    void upload(
        std::span<const IRRender::GPUShapeDescriptor> shapes,
        const IRRender::GPUShapesFrameData &frameData
    ) {
        IR_ASSERT(
            frameData.shapeCount >= 0 &&
                shapes.size() == static_cast<std::size_t>(frameData.shapeCount),
            "Shape descriptor count must match the projection snapshot"
        );
        if (shapes.empty()) {
            reset();
            return;
        }
        if (shapes.size() > capacity_) {
            if (descriptors_.second != nullptr)
                IRRender::destroyResource<IRRender::Buffer>(descriptors_.first);
            capacity_ = IRMath::nextPowerOfTwo(static_cast<std::uint32_t>(shapes.size()));
            descriptors_ = IRRender::createResource<IRRender::Buffer>(
                nullptr,
                capacity_ * sizeof(IRRender::GPUShapeDescriptor),
                IRRender::BUFFER_STORAGE_DYNAMIC
            );
        }
        descriptors_.second->subData(0, shapes.size_bytes(), shapes.data());
        frameData_ = frameData;
    }

    void onDestroy() {
        if (descriptors_.second != nullptr)
            IRRender::destroyResource<IRRender::Buffer>(descriptors_.first);
        descriptors_ = {0, nullptr};
        capacity_ = 0;
        reset();
    }
};

} // namespace IRComponents

#endif /* CANVAS_SHAPE_GEOMETRY_H */
