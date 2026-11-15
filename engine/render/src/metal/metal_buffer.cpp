#include <irreden/render/buffer.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/ir_profile.hpp>

#include <cstring>
#include <span>

namespace IRRender {

// Backings are allocated in whole four-byte words so a buffer's size never
// makes a word-aligned write unrepresentable as a blit. The logical size, which
// bounds every write and read, is unchanged.
namespace {
std::size_t backingBytes(std::size_t size) {
    return (size + 3u) & ~std::size_t{3u};
}
} // namespace

class MetalBufferImpl final : public BufferImpl {
  public:
    MetalBufferImpl(const void *data, std::size_t size)
        : m_size(size == 0 ? 1 : size) {
        m_buffer = metalDevice()->newBuffer(
            static_cast<NS::UInteger>(backingBytes(m_size)),
            MTL::ResourceStorageModeShared
        );
        IR_ASSERT(m_buffer != nullptr, "Failed to create Metal buffer");
        if (data != nullptr && size > 0) {
            std::memcpy(m_buffer->contents(), data, size);
        }
    }

    MetalBufferImpl(
        const void *data,
        std::size_t size,
        BufferTarget target,
        std::uint32_t index
    )
        : MetalBufferImpl(data, size) {
        bindBase(target, index);
    }

    ~MetalBufferImpl() override {
        // Drop any sticky bind slot that still references this handle so a
        // later dispatch cannot re-bind the freed buffer. This is the buffer
        // twin of the texture destructor's untrackMetalTexture call.
        untrackMetalBuffer(m_buffer);
        if (m_buffer != nullptr) {
            m_buffer->release();
            m_buffer = nullptr;
        }
    }

    std::uint32_t getHandle() const override {
        return 0;
    }

    void *getNativeBuffer() const override {
        return m_buffer;
    }

    void subData(std::ptrdiff_t offset, std::size_t size, const void *data) const override {
        if (data == nullptr || size == 0 || offset < 0) {
            return;
        }
        const BufferUploadRange range{offset, size, data};
        subDataRanges({&range, 1});
    }

    void subDataRanges(std::span<const BufferUploadRange> ranges) const override {
        std::size_t writtenBytes = 0;
        std::size_t alignmentBits = 0;
        std::size_t previousEnd = 0;
        for (const BufferUploadRange &range : ranges) {
            IR_ASSERT(
                range.offset_ >= 0 && static_cast<std::size_t>(range.offset_) >= previousEnd,
                "Metal buffer subDataRanges ranges must be sorted and disjoint"
            );
            IR_ASSERT(
                range.data_ != nullptr || range.size_ == 0,
                "Metal buffer subDataRanges range has no data"
            );
            previousEnd = static_cast<std::size_t>(range.offset_) + range.size_;
            IR_ASSERT(
                previousEnd <= m_size,
                "Metal buffer subDataRanges write exceeded buffer size"
            );
            writtenBytes += range.size_;
            if (range.size_ != 0) {
                alignmentBits |= static_cast<std::size_t>(range.offset_) | range.size_;
            }
        }
        if (writtenBytes == 0) {
            return;
        }

        // No queued encoder can observe this allocation yet.
        if (!wasMetalBufferEncoded(m_buffer)) {
            writeRanges(m_buffer, ranges);
            return;
        }

        if (writtenBytes == m_size) {
            replaceBacking(ranges);
            return;
        }

        // macOS buffer blits require four-byte offsets and lengths, and widening
        // a span to word boundaries would overwrite bytes a queued shader may
        // own. Waiting is the only correct way to land such a write.
        if ((alignmentBits & 3u) != 0) {
            waitAndWriteInPlace(ranges);
            return;
        }

        stagePatch(ranges, writtenBytes);
    }

    void getSubData(std::ptrdiff_t offset, std::size_t size, void *data) const override {
        if (data == nullptr || size == 0 || offset < 0) {
            return;
        }
        IR_ASSERT(
            static_cast<std::size_t>(offset) + size <= m_size,
            "Metal buffer getSubData read exceeded buffer size"
        );
        std::memcpy(data, static_cast<std::uint8_t *>(m_buffer->contents()) + offset, size);
    }

    void bindRange(
        BufferTarget target,
        std::uint32_t index,
        std::ptrdiff_t offset,
        std::size_t
    ) override {
        bindMetalBuffer(target, index, m_buffer, static_cast<NS::UInteger>(offset));
    }

    void bindBase(BufferTarget target, std::uint32_t index) override {
        bindMetalBuffer(target, index, m_buffer, 0);
    }

    void *mapRange(std::ptrdiff_t offset, std::size_t, std::uint32_t) override {
        return static_cast<std::uint8_t *>(m_buffer->contents()) + offset;
    }

    void unmap() override {}

    void resize(std::size_t size) override {
        m_size = size == 0 ? 1 : size;
        auto *newBuffer = metalDevice()->newBuffer(
            static_cast<NS::UInteger>(backingBytes(m_size)),
            MTL::ResourceStorageModeShared
        );
        IR_ASSERT(newBuffer != nullptr, "Failed to resize Metal buffer");
        adoptBacking(newBuffer);
    }

  private:
    // The batch covers every byte, so nothing in the old backing survives and a
    // fresh backing needs no GPU copy.
    void replaceBacking(std::span<const BufferUploadRange> ranges) const {
        auto *newBuffer = metalDevice()->newBuffer(
            static_cast<NS::UInteger>(backingBytes(m_size)),
            MTL::ResourceStorageModeShared
        );
        IR_ASSERT(newBuffer != nullptr, "Failed to replace Metal buffer in subDataRanges");
        writeRanges(newBuffer, ranges);
        ++metalBufferWriteStats().fullReplacements_;
        adoptBacking(newBuffer);
    }

    // The ranges are copied into the existing backing on the GPU timeline, so
    // encoders created before this call read the old bytes, encoders created
    // after it read the new ones, and bytes outside the ranges are never
    // touched. A CPU read sees the new bytes once the submission completes.
    void stagePatch(std::span<const BufferUploadRange> ranges, std::size_t writtenBytes) const {
        const MetalStagingBufferSlice staging = reserveMetalStaging(writtenBytes);
        std::uint8_t *packed = staging.contents();
        for (const BufferUploadRange &range : ranges) {
            if (range.size_ != 0) {
                std::memcpy(packed, range.data_, range.size_);
                packed += range.size_;
            }
        }

        auto *blit = createMetalBlitEncoder();
        IR_ASSERT(blit != nullptr, "Metal buffer patch needs an open command buffer");
        NS::UInteger sourceOffset = staging.offset_;
        for (const BufferUploadRange &range : ranges) {
            if (range.size_ == 0) {
                continue;
            }
            blit->copyFromBuffer(
                staging.buffer_,
                sourceOffset,
                m_buffer,
                static_cast<NS::UInteger>(range.offset_),
                static_cast<NS::UInteger>(range.size_)
            );
            sourceOffset += static_cast<NS::UInteger>(range.size_);
        }
        blit->endEncoding();
        // A later CPU write this frame must not land before the queued copy.
        markMetalBufferEncoded(m_buffer);

        BufferWriteStats &stats = metalBufferWriteStats();
        ++stats.stagedPatches_;
        stats.stagedBytes_ += writtenBytes;
        if (staging.allocatedBytes_ != 0) {
            ++stats.partialWriteAllocations_;
            stats.partialWriteAllocatedBytes_ += staging.allocatedBytes_;
        }
    }

    void waitAndWriteInPlace(std::span<const BufferUploadRange> ranges) const {
        ++metalBufferWriteStats().midFrameWaits_;
        if (!m_reportedMidFrameWait) {
            m_reportedMidFrameWait = true;
            IRE_LOG_WARN(
                "Metal buffer ({} bytes) waited for the GPU: a partial write to a buffer the "
                "GPU may still read is not word-aligned",
                m_size
            );
        }
        device()->finish();
        writeRanges(m_buffer, ranges);
    }

    // Queued encoders keep the old allocation until the GPU has drained it.
    void adoptBacking(MTL::Buffer *newBuffer) const {
        MTL::Buffer *oldBuffer = m_buffer;
        m_buffer = newBuffer;
        replaceMetalBufferInBindings(oldBuffer, m_buffer);
        deferReleaseMetalBuffer(oldBuffer);
    }

    static void writeRanges(MTL::Buffer *buffer, std::span<const BufferUploadRange> ranges) {
        auto *dst = static_cast<std::uint8_t *>(buffer->contents());
        for (const BufferUploadRange &range : ranges) {
            if (range.size_ != 0) {
                std::memcpy(dst + range.offset_, range.data_, range.size_);
            }
        }
    }

    // mutable: subData is const on the BufferImpl interface but a full
    // replacement of a buffer queued work may still read swaps the backing.
    mutable MTL::Buffer *m_buffer = nullptr;
    std::size_t m_size = 0;
    mutable bool m_reportedMidFrameWait = false;
};

std::unique_ptr<BufferImpl> createBufferImpl(const void *data, std::size_t size, std::uint32_t) {
    return std::make_unique<MetalBufferImpl>(data, size);
}

std::unique_ptr<BufferImpl> createBufferImpl(
    const void *data,
    std::size_t size,
    std::uint32_t,
    BufferTarget target,
    std::uint32_t index
) {
    return std::make_unique<MetalBufferImpl>(data, size, target, index);
}

} // namespace IRRender
