#ifndef CANVAS_SHAPE_GEOMETRY_H
#define CANVAS_SHAPE_GEOMETRY_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace IRComponents {

// Descriptor indices are canvas-local and remain valid until its next shape upload.
// The projection snapshot describes that upload, not the currently bound global UBO.
struct CanvasShapeGeometry {
    std::pair<IRRender::ResourceId, IRRender::Buffer *> descriptors_{0, nullptr};
    std::size_t capacity_ = 0;
    IRRender::GPUShapesFrameData frameData_{};
    std::pair<IRRender::ResourceId, IRRender::Buffer *> tiles_{0, nullptr};
    std::size_t tileCapacity_ = 0;
    std::size_t tileCount_ = 0;
    std::pair<IRRender::ResourceId, IRRender::Buffer *> sampleOwners_{0, nullptr};
    std::size_t ownerCapacityBytes_ = 0;
    IRMath::ivec2 ownerSize_{};

    bool samplesValid() const {
        return m_samplesValid;
    }

    void invalidateSamples() const {
        m_samplesValid = false;
    }

    void publishSamples(bool unblended) {
        IR_ASSERT(
            frameData_.shapeCount > 0 && tileCount_ > 0 && ownerSize_.x > 0 && ownerSize_.y > 0,
            "Published shape samples require a complete submission"
        );
        m_samplesValid = unblended;
    }

    void reset() {
        invalidateSamples();
        frameData_ = {};
        tileCount_ = 0;
        ownerSize_ = IRMath::ivec2(0);
    }

    void upload(
        std::span<const IRRender::GPUShapeDescriptor> shapes,
        const IRRender::GPUShapesFrameData &frameData
    ) {
        invalidateSamples();
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

    void uploadTiles(std::span<const IRRender::ShapeTileDescriptor> tiles) {
        invalidateSamples();
        if (tiles.size() > tileCapacity_) {
            if (tiles_.second != nullptr)
                IRRender::destroyResource<IRRender::Buffer>(tiles_.first);
            tileCapacity_ = IRMath::nextPowerOfTwo(static_cast<std::uint32_t>(tiles.size()));
            tiles_ = IRRender::createResource<IRRender::Buffer>(
                nullptr,
                tileCapacity_ * sizeof(IRRender::ShapeTileDescriptor),
                IRRender::BUFFER_STORAGE_DYNAMIC
            );
        }
        if (!tiles.empty())
            tiles_.second->subData(0, tiles.size_bytes(), tiles.data());
        tileCount_ = tiles.size();
    }

    void prepareSampleOwners(IRMath::ivec2 size) {
        invalidateSamples();
        IR_ASSERT(size.x > 0 && size.y > 0, "Shape owner dimensions must be positive");
        const auto bytes = std::size_t(size.x) * std::size_t(size.y) * sizeof(std::uint32_t);
        if (bytes > ownerCapacityBytes_) {
            if (sampleOwners_.second != nullptr)
                IRRender::destroyResource<IRRender::Buffer>(sampleOwners_.first);
            sampleOwners_ = IRRender::createResource<IRRender::Buffer>(
                nullptr,
                bytes,
                IRRender::BUFFER_STORAGE_DYNAMIC
            );
            ownerCapacityBytes_ = bytes;
        }
        // The fill must follow previous shader writes to a retained allocation on OpenGL.
        IRRender::device()->memoryBarrier(IRRender::BarrierType::ALL);
        IRRender::device()->fillBuffer(sampleOwners_.second, bytes, 0xFF);
        ownerSize_ = size;
    }

    void onDestroy() {
        if (descriptors_.second != nullptr)
            IRRender::destroyResource<IRRender::Buffer>(descriptors_.first);
        descriptors_ = {0, nullptr};
        capacity_ = 0;
        if (tiles_.second != nullptr)
            IRRender::destroyResource<IRRender::Buffer>(tiles_.first);
        tiles_ = {0, nullptr};
        tileCapacity_ = 0;
        if (sampleOwners_.second != nullptr)
            IRRender::destroyResource<IRRender::Buffer>(sampleOwners_.first);
        sampleOwners_ = {0, nullptr};
        ownerCapacityBytes_ = 0;
        reset();
    }

  private:
    // Const canvas clears mutate GPU content and invalidate this derived reference state.
    mutable bool m_samplesValid = false;
};

} // namespace IRComponents

#endif /* CANVAS_SHAPE_GEOMETRY_H */
