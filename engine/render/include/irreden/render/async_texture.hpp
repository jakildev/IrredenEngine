#ifndef ASYNC_TEXTURE_H
#define ASYNC_TEXTURE_H

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/ir_render_enums.hpp>

#include <cstdint>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace IRRender {

class Texture2D;

struct DecodedImage {
    std::vector<uint8_t> pixels_;
    int width_ = 0;
    int height_ = 0;
};

class AsyncTextureHandle {
  public:
    AsyncTextureHandle() = default;
    explicit AsyncTextureHandle(std::future<DecodedImage> future);

    AsyncTextureHandle(AsyncTextureHandle &&) = default;
    AsyncTextureHandle &operator=(AsyncTextureHandle &&) = default;
    AsyncTextureHandle(const AsyncTextureHandle &) = delete;
    AsyncTextureHandle &operator=(const AsyncTextureHandle &) = delete;

    bool valid() const;
    bool isReady() const;
    DecodedImage take();

  private:
    std::future<DecodedImage> m_future;
};

/// Decode an image file on a background thread. The returned handle
/// resolves to a DecodedImage containing the raw RGBA pixel data.
/// GPU upload must happen on the main thread via uploadDecodedImage.
AsyncTextureHandle loadImageAsync(const std::string &path);

/// Upload pre-decoded pixel data to a GPU Texture2D. Must be called
/// from the main thread (GL/Metal context affinity).
std::pair<ResourceId, Texture2D *> uploadDecodedImage(
    const DecodedImage &img,
    TextureWrap wrap = TextureWrap::CLAMP_TO_EDGE,
    TextureFilter filter = TextureFilter::NEAREST,
    int alignment = 4
);

} // namespace IRRender

#endif /* ASYNC_TEXTURE_H */
