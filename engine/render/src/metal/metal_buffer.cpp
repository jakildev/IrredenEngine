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
        m_handle = registerMetalBufferHandle(m_buffer);
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
        unregisterMetalBufferHandle(m_handle);
        if (m_buffer != nullptr) {
            m_buffer->release();
            m_buffer = nullptr;
        }
    }

    std::uint32_t getHandle() const override {
        return m_handle;
    }

    void subData(std::ptrdiff_t offset, std::size_t size, const void *data) const override {
        if (data == nullptr || size == 0 || offset < 0) {
            return;
        }
        IR_ASSERT(
            static_cast<std::size_t>(offset) + size <= m_size,
            "Metal buffer subData write exceeded buffer size"
        );
        std::memcpy(static_cast<std::uint8_t *>(m_buffer->contents()) + offset, data, size);
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

  private:
    MTL::Buffer *m_buffer = nullptr;
    std::size_t m_size = 0;
    std::uint32_t m_handle = 0;
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
