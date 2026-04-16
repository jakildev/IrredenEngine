#include <irreden/render/buffer.hpp>

#include <utility>

namespace IRRender {

std::unique_ptr<BufferImpl> createBufferImpl(const void *data, std::size_t size, std::uint32_t flags);
std::unique_ptr<BufferImpl> createBufferImpl(
    const void *data,
    std::size_t size,
    std::uint32_t flags,
    BufferTarget target,
    std::uint32_t index
);

Buffer::Buffer(const void *data, std::size_t size, std::uint32_t flags)
    : m_impl(createBufferImpl(data, size, flags)) {}

Buffer::Buffer(
    const void *data,
    std::size_t size,
    std::uint32_t flags,
    BufferTarget target,
    std::uint32_t index
)
    : m_impl(createBufferImpl(data, size, flags, target, index)) {}

Buffer::~Buffer() = default;

Buffer::Buffer(Buffer &&other) noexcept = default;

Buffer &Buffer::operator=(Buffer &&other) noexcept = default;

std::uint32_t Buffer::getHandle() const {
    return m_impl->getHandle();
}

void *Buffer::getNativeBuffer() const {
    return m_impl->getNativeBuffer();
}

void Buffer::subData(std::ptrdiff_t offset, std::size_t size, const void *data) const {
    m_impl->subData(offset, size, data);
}

void Buffer::getSubData(std::ptrdiff_t offset, std::size_t size, void *data) const {
    m_impl->getSubData(offset, size, data);
}

void Buffer::bindRange(
    BufferTarget target, std::uint32_t index, std::ptrdiff_t offset, std::size_t size
) {
    m_impl->bindRange(target, index, offset, size);
}

void Buffer::bindBase(BufferTarget target, std::uint32_t index) {
    m_impl->bindBase(target, index);
}

void *Buffer::mapRange(std::ptrdiff_t offset, std::size_t length, std::uint32_t accessFlags) {
    return m_impl->mapRange(offset, length, accessFlags);
}

void Buffer::unmap() {
    m_impl->unmap();
}

} // namespace IRRender