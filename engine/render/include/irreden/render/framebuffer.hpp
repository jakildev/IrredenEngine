#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <irreden/ir_math.hpp>

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/texture.hpp>

#include <memory>

using namespace IRMath;

namespace IRRender {

class FramebufferImpl {
  public:
    virtual ~FramebufferImpl() = default;
    virtual void bind() const = 0;
    virtual void unbind() = 0;
    virtual void clear() const = 0;
};

class Framebuffer {
  public:
    Framebuffer(
        ivec2 resolution,
        ivec2 extraPixelBuffer,
        TextureFormat formatColor,
        TextureFormat formatDepthStencil
    );
    ~Framebuffer();
    Framebuffer(Framebuffer &&other) noexcept = delete;
    Framebuffer &operator=(Framebuffer &&other) noexcept = delete;
    Framebuffer(const Framebuffer &) = delete;
    Framebuffer &operator=(const Framebuffer &) = delete;

    void bind() const;
    void unbind();
    void clear() const;

    inline const Texture2D &getTextureColor() const {
        return m_textureColor;
    }
    inline const Texture2D &getTextureDepth() const {
        return m_textureDepth;
    }
    inline const ivec2 getResolution() const {
        return m_resolution;
    }
    inline const ivec2 getResolutionPlusBuffer() const {
        return m_resolutionPlusBuffer;
    }

  private:
    const ivec2 m_resolution;
    const ivec2 m_extraPixelBuffer;
    const ivec2 m_resolutionPlusBuffer;
    Texture2D m_textureColor;
    Texture2D m_textureDepth;
    std::unique_ptr<FramebufferImpl> m_impl;
};

} // namespace IRRender

#endif /* FRAMEBUFFER_H */
