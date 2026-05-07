#ifndef COMPONENT_CANVAS_LIGHT_VOLUME_H
#define COMPONENT_CANVAS_LIGHT_VOLUME_H

// World-space 3D light volume populated by COMPUTE_LIGHT_VOLUME and
// consumed by LIGHTING_TO_TRIXEL. Required on every canvas that
// participates in LIGHTING_TO_TRIXEL — the system's archetype filter
// lists it alongside C_CanvasAOTexture and C_CanvasSunShadow, so a
// canvas missing it silently skips the entire lighting pass (including
// AO × shadow). Pair with C_TriangleCanvasTextures + C_OccupancyGrid;
// if no emitters exist, the volume stays zero-filled and the additive
// contribution is a no-op.
//
// The volume is centered on `m_worldOriginVoxel` (defaults to `(0,0,0)`)
// and covers `[origin - kLightVolumeHalfExtent, origin + kLightVolumeHalfExtent)`
// voxels per axis at 1:1 voxel-per-texel resolution. Phase 1c (#360)
// wired COMPUTE_LIGHT_VOLUME to track the iso camera each frame so the
// addressable region follows the visible scene instead of staying
// pinned to world origin. Geometry outside the current range is sampled
// with CLAMP_TO_EDGE — light contributions outside the box read zero.
//
// Sized smaller than `C_OccupancyGrid` (256³) on purpose: the
// 128³ × RGBA8 footprint (8 MiB per buffer) keeps GPU storage bounded
// and the dilation chain's per-iteration cost low. Light sources
// placed outside the camera-anchored extent are still skipped on the
// CPU before the SSBO upload (Phase 1b / issue #362), with a one-shot
// warning per unique offending origin in `system_compute_light_volume`
// so silent clamping never recurs.
//
// Two textures (`textureRead_` / `textureWrite_`) form a ping-pong pair
// for the GPU jump-flood propagation passes (Phase 1a / issue #359).
// The producer system swaps them after each propagation iteration so
// `getReadTexture()` always yields the latest result for the
// downstream lighting consumer to sample.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <cstdint>
#include <utility>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

constexpr int kLightVolumeSize = 128;
constexpr int kLightVolumeHalfExtent = kLightVolumeSize / 2;

struct C_CanvasLightVolume {
    std::pair<ResourceId, Texture3D *> textureRead_;
    std::pair<ResourceId, Texture3D *> textureWrite_;

    C_CanvasLightVolume()
        : textureRead_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , textureWrite_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )} {}

    void onDestroy() {
        IRRender::destroyResource<Texture3D>(textureRead_.first);
        IRRender::destroyResource<Texture3D>(textureWrite_.first);
    }

    /// Texture currently holding the latest propagated light result.
    /// Sampled by the downstream `LIGHTING_TO_TRIXEL` consumer.
    Texture3D *getReadTexture() const {
        IR_ASSERT(
            textureRead_.second != nullptr,
            "C_CanvasLightVolume::getReadTexture() called on default-"
            "constructed instance — must be constructed via the "
            "default ctor (which allocates the GPU textures)."
        );
        return textureRead_.second;
    }

    /// Scratch texture written by the next propagate pass; swapped to
    /// the read side via `swap()` after the dispatch completes.
    Texture3D *getWriteTexture() const {
        IR_ASSERT(
            textureWrite_.second != nullptr,
            "C_CanvasLightVolume::getWriteTexture() called on default-"
            "constructed instance."
        );
        return textureWrite_.second;
    }

    /// Promote `textureWrite_` to the read side after a propagation
    /// pass. Subsequent reads (`getReadTexture()`) see the new contents
    /// while the old read texture becomes the next pass's scratch.
    void swap() {
        std::swap(textureRead_, textureWrite_);
    }

    /// Backwards-compatible alias for the previously-named accessor;
    /// still used by `LIGHTING_TO_TRIXEL`'s sampler binding. Yields
    /// the read-side texture so the consumer always sees the latest
    /// propagated state.
    Texture3D *getTexture() const {
        return getReadTexture();
    }

    /// World voxel the volume is centered on (Phase 1c / #360).
    /// Defaults to `(0,0,0)` so static-camera scenes keep the original
    /// world-origin centering.
    ivec3 worldOriginVoxel() const {
        return m_worldOriginVoxel;
    }

    /// Update the volume's world anchor. The producer system calls this
    /// once per frame from the iso camera; downstream sampling shaders
    /// subtract the anchor before indexing the volume.
    void setWorldOriginVoxel(const ivec3 &origin) {
        m_worldOriginVoxel = origin;
    }

  private:
    ivec3 m_worldOriginVoxel{0, 0, 0};
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_LIGHT_VOLUME_H */
