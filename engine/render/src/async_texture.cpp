#include <irreden/render/async_texture.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/image_data.hpp>
#include <irreden/render/texture.hpp>

namespace IRRender {

AsyncTextureHandle::AsyncTextureHandle(std::future<DecodedImage> future)
    : m_future(std::move(future)) {}

bool AsyncTextureHandle::valid() const {
    return m_future.valid();
}

bool AsyncTextureHandle::isReady() const {
    return m_future.valid() &&
           m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

DecodedImage AsyncTextureHandle::take() {
    IR_ASSERT(m_future.valid(), "AsyncTextureHandle::take() called on invalid handle");
    return m_future.get();
}

static DecodedImage decodeImageFile(const std::string &path) {
    ImageData img(path.c_str());
    DecodedImage result;
    result.width_ = img.width_;
    result.height_ = img.height_;
    constexpr int kBytesPerRGBAPixel = 4; // stbi_load force-channel = 4
    const std::size_t byteCount =
        static_cast<std::size_t>(img.width_) * img.height_ * kBytesPerRGBAPixel;
    result.pixels_.assign(img.data_, img.data_ + byteCount);
    return result;
}

AsyncTextureHandle loadImageAsync(const std::string &path) {
    return AsyncTextureHandle(
        std::async(std::launch::async, decodeImageFile, path)
    );
}

std::pair<ResourceId, Texture2D *> uploadDecodedImage(
    const DecodedImage &img, TextureWrap wrap, TextureFilter filter, int alignment
) {
    auto [id, tex] = createResource<Texture2D>(
        TextureKind::TEXTURE_2D,
        static_cast<unsigned int>(img.width_),
        static_cast<unsigned int>(img.height_),
        TextureFormat::RGBA8,
        wrap,
        filter,
        alignment
    );
    tex->subImage2D(
        0,
        0,
        img.width_,
        img.height_,
        PixelDataFormat::RGBA,
        PixelDataType::UNSIGNED_BYTE,
        img.pixels_.data()
    );
    return {id, tex};
}

} // namespace IRRender
