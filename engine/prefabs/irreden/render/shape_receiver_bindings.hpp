#ifndef SHAPE_RECEIVER_BINDINGS_H
#define SHAPE_RECEIVER_BINDINGS_H

#include <irreden/render/components/canvas_shape_geometry.hpp>

namespace IRPrefab::detail {

inline void bindShapeReceiver(
    const IRComponents::CanvasShapeGeometry &geometry,
    IRMath::ivec2 canvasSize,
    IRRender::Buffer &frame
) {
    using namespace IRRender;
    IR_ASSERT(
        geometry.samplesValid() && geometry.ownerSize_ == canvasSize,
        "Shape receiver bindings require valid owners matching the canvas extent"
    );
    frame.subData(0, sizeof(GPUShapesFrameData), &geometry.frameData_);
    frame.bindBase(BufferTarget::UNIFORM, kBufferIndex_ShapesFrameData);
    geometry.descriptors_.second->bindBase(
        BufferTarget::SHADER_STORAGE,
        kBufferIndex_ShapeDescriptors
    );
    geometry.sampleOwners_.second->bindBase(
        BufferTarget::SHADER_STORAGE,
        kBufferIndex_ShapeSampleOwners
    );
    geometry.tiles_.second->bindBase(
        BufferTarget::SHADER_STORAGE,
        kBufferIndex_ShapeTileDescriptors
    );
}

inline void restoreShapeReceiver(
    IRRender::Buffer &fallback, IRRender::Buffer *producerFrame, IRRender::Buffer *animationParams
) {
    using namespace IRRender;
    if (producerFrame != nullptr)
        producerFrame->bindBase(BufferTarget::UNIFORM, kBufferIndex_ShapesFrameData);
    // Persistent bindings must not retain canvas allocations across destruction.
    fallback.bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_ShapeDescriptors);
    fallback.bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_ShapeTileDescriptors);
    (animationParams != nullptr ? animationParams : &fallback)
        ->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_AnimationParams);
}

} // namespace IRPrefab::detail

#endif /* SHAPE_RECEIVER_BINDINGS_H */
