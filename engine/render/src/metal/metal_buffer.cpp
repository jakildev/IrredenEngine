#include <irreden/render/buffer.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/ir_profile.hpp>

#include <cstring>

namespace IRRender {

class MetalBufferImpl final : public BufferImpl {
  public:
    MetalBufferImpl(const void *data, std::size_t size)
        : m_size(size == 0 ? 1 : size) {
        m_buffer = metalDevice()->newBuffer(
            static_cast<NS::UInteger>(m_size),
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
        IR_ASSERT(
            static_cast<std::size_t>(offset) + size <= m_size,
            "Metal buffer subData write exceeded buffer size"
        );

        // Orphan-on-write: see deferReleaseMetalBuffer in metal_runtime.cpp.
        auto *newBuffer = metalDevice()->newBuffer(
            static_cast<NS::UInteger>(m_size),
            MTL::ResourceStorageModeShared
        );
        IR_ASSERT(newBuffer != nullptr, "Failed to orphan Metal buffer in subData");

        std::uint8_t *src = static_cast<std::uint8_t *>(m_buffer->contents());
        std::uint8_t *dst = static_cast<std::uint8_t *>(newBuffer->contents());
        const std::size_t writeOffset = static_cast<std::size_t>(offset);
        if (writeOffset > 0) {
            std::memcpy(dst, src, writeOffset);
        }
        std::memcpy(dst + writeOffset, data, size);
        const std::size_t tailStart = writeOffset + size;
        if (tailStart < m_size) {
            std::memcpy(dst + tailStart, src + tailStart, m_size - tailStart);
        }

        MTL::Buffer *oldBuffer = m_buffer;
        m_buffer = newBuffer;
        replaceMetalBufferInBindings(oldBuffer, m_buffer);
        deferReleaseMetalBuffer(oldBuffer);
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

  private:
    // mutable: subData is const on the BufferImpl interface but orphans the
    // backing buffer on every write (see subData).
    mutable MTL::Buffer *m_buffer = nullptr;
    std::size_t m_size = 0;
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
