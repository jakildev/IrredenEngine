#ifndef IR_PREFAB_DEPTH_TESTED_OWNER_H
#define IR_PREFAB_DEPTH_TESTED_OWNER_H

// Whole-frame form of the per-axis scatter's depth-tested owner rule, for
// probes. The scatter writes colour, depth and the owner id together; every
// later main-framebuffer draw writes colour and depth only. So the owner
// attachment at the end of a frame names the last scatter winner at each
// pixel, and that winner is still shown only where the pixel's depth texel is
// bit-identical to its value right after the scatter draws. The cursor's
// `c_peraxis_hover_resolve` applies the same comparison at one pixel.
//
// While armed, `TRIXEL_TO_FRAMEBUFFER` reads the whole depth attachment back
// between the scatter and the first later draw, on every frame it scatters.
// That is a full GPU flush per frame: arm it only for a scripted diagnostic,
// never in an ordinary run. Unarmed, a frame pays one flag test.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/prefab_system_probe.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>

#include <cstddef>
#include <cstring>
#include <vector>

namespace IRPrefab::DepthTestedOwner {

struct Pixel {
    // The scatter owner still shown here; null where no scatter draw won the
    // pixel or later content covered it.
    IREntity::EntityId owner_ = IREntity::kNullEntity;
    // The scatter winner later content covered; null unless covered.
    IREntity::EntityId coveredOwner_ = IREntity::kNullEntity;
    // Final composite depth, normalized as the depth attachment stores it.
    float depth_ = 1.0f;
};

enum class ReadStatus { OK, NO_COMPOSITE, NOT_ARMED, NO_CAPTURE_THIS_FRAME, SIZE_MISMATCH };

inline const char *readStatusName(ReadStatus status) {
    switch (status) {
    case ReadStatus::OK:
        return "ok";
    case ReadStatus::NO_COMPOSITE:
        return "no-composite-system";
    case ReadStatus::NOT_ARMED:
        return "capture-not-armed";
    case ReadStatus::NO_CAPTURE_THIS_FRAME:
        return "no-capture-from-newest-composite";
    case ReadStatus::SIZE_MISMATCH:
        return "capture-size-mismatch";
    }
    return "unknown";
}

// The comparison itself, free of the device. Inputs are the owner attachment's
// RG32UI words, the depth attachment right after the scatter and at frame end,
// all `size.x * size.y` texels in texture row order; @p pixels comes back in
// top-left pixel order (`compositeDepthTextureRow` flips each row).
inline void resolve(
    const IRMath::uvec2 *ownerWords,
    const float *scatterDepth,
    const float *finalDepth,
    IRMath::ivec2 size,
    std::vector<Pixel> &pixels
) {
    const std::size_t width = static_cast<std::size_t>(IRMath::max(size.x, 0));
    const std::size_t height = static_cast<std::size_t>(IRMath::max(size.y, 0));
    pixels.assign(width * height, Pixel{});
    for (int y = 0; y < size.y; ++y) {
        const std::size_t textureRow =
            static_cast<std::size_t>(IRRender::compositeDepthTextureRow(y, size.y)) * width;
        for (std::size_t x = 0; x < width; ++x) {
            const std::size_t texel = textureRow + x;
            Pixel &pixel = pixels[static_cast<std::size_t>(y) * width + x];
            pixel.depth_ = finalDepth[texel];
            const auto owner =
                static_cast<IREntity::EntityId>(IRRender::decodeCarrierEntityId(ownerWords[texel]));
            if (owner == IREntity::kNullEntity)
                continue;
            if (std::memcmp(&scatterDepth[texel], &finalDepth[texel], sizeof(float)) == 0)
                pixel.owner_ = owner;
            else
                pixel.coveredOwner_ = owner;
        }
    }
}

inline IRSystem::System<IRSystem::TRIXEL_TO_FRAMEBUFFER> *systemOrNull() {
    return IRPrefab::detail::systemOrNull<IRSystem::TRIXEL_TO_FRAMEBUFFER>();
}

inline void setArmed(bool armed) {
    if (auto *system = systemOrNull())
        system->scatterDepthCaptureArmed_ = armed;
}

// Reads the newest composite's depth-tested owners and final depth into
// @p pixels (top-left order) and its size into @p size. Call after that
// composite ran this RENDER tick. Waits for the GPU. Anything but `OK` leaves
// @p pixels empty: there is no fallback to the raw owner attachment.
inline ReadStatus read(std::vector<Pixel> &pixels, IRMath::ivec2 &size) {
    pixels.clear();
    size = IRMath::ivec2(0);
    const auto *system = systemOrNull();
    if (system == nullptr)
        return ReadStatus::NO_COMPOSITE;
    if (!system->scatterDepthCaptureArmed_)
        return ReadStatus::NOT_ARMED;
    if (!system->scatterDepthCaptured_ || system->scatterDepthCaptureRenderTick_ !=
                                              IRSystem::getEventTickCount(IRTime::Events::RENDER))
        return ReadStatus::NO_CAPTURE_THIS_FRAME;
    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const IRMath::ivec2 resolution = framebuffer.getResolutionPlusBuffer();
    if (resolution != system->scatterDepthCaptureSize_)
        return ReadStatus::SIZE_MISMATCH;
    const std::size_t count =
        static_cast<std::size_t>(resolution.x) * static_cast<std::size_t>(resolution.y);
    std::vector<IRMath::uvec2> ownerWords(count);
    std::vector<float> finalDepth(count);
    IRRender::device()->finish();
    framebuffer.framebuffer_.second->getTextureEntityIds().getSubImage2D(
        0,
        0,
        resolution.x,
        resolution.y,
        IRRender::PixelDataFormat::RG_INTEGER,
        IRRender::PixelDataType::UINT32,
        ownerWords.data()
    );
    framebuffer.getTextureDepth().getSubImage2D(
        0,
        0,
        resolution.x,
        resolution.y,
        IRRender::PixelDataFormat::DEPTH_COMPONENT,
        IRRender::PixelDataType::FLOAT32,
        finalDepth.data()
    );
    resolve(
        ownerWords.data(),
        system->scatterDepthCapture_.data(),
        finalDepth.data(),
        resolution,
        pixels
    );
    size = resolution;
    return ReadStatus::OK;
}

} // namespace IRPrefab::DepthTestedOwner

#endif /* IR_PREFAB_DEPTH_TESTED_OWNER_H */
