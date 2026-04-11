#ifndef BUFFER_H
#define BUFFER_H

#include <irreden/render/ir_render_enums.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace IRRender {

class BufferImpl {
  public:
    virtual ~BufferImpl() = default;
    virtual std::uint32_t getHandle() const = 0;
    // Native backend object (MTL::Buffer* on Metal, nullptr on OpenGL where
    // getHandle is canonical). Mirrors Texture2DImpl::getNativeTexture.
    virtual void *getNativeBuffer() const = 0;
    virtual void subData(std::ptrdiff_t offset, std::size_t size, const void *data) const = 0;
    virtual void getSubData(std::ptrdiff_t offset, std::size_t size, void *data) const = 0;
    virtual void bindRange(BufferTarget target, std::uint32_t index, std::ptrdiff_t offset, std::size_t size) = 0;
    virtual void bindBase(BufferTarget target, std::uint32_t index) = 0;
    virtual void *mapRange(std::ptrdiff_t offset, std::size_t length, std::uint32_t accessFlags) = 0;
    virtual void unmap() = 0;
};

class Buffer {
  public:
    Buffer(const void *data, std::size_t size, std::uint32_t flags);
    Buffer(
        const void *data,
        std::size_t size,
        std::uint32_t flags,
        BufferTarget target,
        std::uint32_t index
    );
    ~Buffer();
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;

    std::uint32_t getHandle() const;
    void *getNativeBuffer() const;
    void subData(std::ptrdiff_t offset, std::size_t size, const void *data) const;
    void getSubData(std::ptrdiff_t offset, std::size_t size, void *data) const;
    void bindRange(BufferTarget target, std::uint32_t index, std::ptrdiff_t offset, std::size_t size);
    void bindBase(BufferTarget target, std::uint32_t index);
    void *mapRange(std::ptrdiff_t offset, std::size_t length, std::uint32_t accessFlags);
    void unmap();

  private:
    std::unique_ptr<BufferImpl> m_impl;
};

} // namespace IRRender

#endif /* BUFFER_H */
