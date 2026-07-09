#ifndef COMPONENT_CANVAS_LIGHT_VOLUME_H
#define COMPONENT_CANVAS_LIGHT_VOLUME_H

// World-space 3D light volume populated by COMPUTE_LIGHT_VOLUME and
// consumed by LIGHTING_TO_TRIXEL. Required on every canvas that
// participates in LIGHTING_TO_TRIXEL — the system's archetype filter
// lists it alongside C_CanvasAOTexture and C_CanvasSunShadow, so a
// canvas missing it silently skips the entire lighting pass (including
// AO × shadow). Pair with C_TriangleCanvasTextures; if no emitters
// exist, the volume stays zero-filled and the additive contribution
// is a no-op. The light-occlusion SSBO that gates propagation is
// produced engine-wide by `BUILD_LIGHT_OCCLUSION_GRID` and does not
// need to be attached per canvas.
//
// The volume is centered on `m_worldOriginVoxel` (defaults to `(0,0,0)`)
// and covers `[origin - kLightVolumeHalfExtent, origin + kLightVolumeHalfExtent)`
// voxels per axis at 1:1 voxel-per-texel resolution. Phase 1c (#360)
// wired COMPUTE_LIGHT_VOLUME to track the iso camera each frame so the
// addressable region follows the visible scene instead of staying
// pinned to world origin. Geometry outside the current range is sampled
// with CLAMP_TO_EDGE — light contributions outside the box read zero.
//
// Sized smaller than the light-occlusion SSBO grid (256³) on purpose: the
// 128³ × RGBA8 footprint (8 MiB per buffer) keeps GPU storage bounded
// and the dilation chain's per-iteration cost low. Light sources placed
// outside the camera-anchored extent seed the clamped window boundary at
// a distance-discounted residual (exact under the propagate pass's
// Manhattan metric — see `gatherLightSources`), so their in-window
// contribution is correct and fades continuously; only lights whose
// residual budget cannot reach the window are skipped. The perf HUD's
// CULL block reports seeded/eligible counts.
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

    // Winning-light ID ping-pong pair (#2318, L2). Parallel to the color
    // pair: the seed pass writes `lightIndex + 1` (0 = no light) and the
    // propagate pass carries the winning candidate's ID alongside its
    // color/alpha, so `c_lighting_to_trixel` can recover which light lit a
    // cell and apply that light's SPOT cone factor. R16UI holds the full
    // 1..kLightVolumeMaxSources (256) index range plus the 0 sentinel;
    // NEAREST because integer IDs must never interpolate (the color pair
    // keeps LINEAR — two textures, one sample coordinate). Swapped in
    // lockstep with the color pair in `swap()`.
    std::pair<ResourceId, Texture3D *> idTextureRead_;
    std::pair<ResourceId, Texture3D *> idTextureWrite_;

    // LINEAR so the lighting consumer's sampler3D read interpolates
    // between volume cells — softens the integer Manhattan falloff
    // shells into a smooth gradient. The propagate/seed passes bind the
    // textures as images (imageLoad/imageStore), which bypass filtering,
    // so the dilation math itself is unaffected.
    C_CanvasLightVolume()
        : textureRead_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::LINEAR
          )}
        , textureWrite_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::LINEAR
          )}
        , idTextureRead_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::R16UI,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , idTextureWrite_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::R16UI,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )} {}

    void onDestroy() {
        IRRender::destroyResource<Texture3D>(textureRead_.first);
        IRRender::destroyResource<Texture3D>(textureWrite_.first);
        IRRender::destroyResource<Texture3D>(idTextureRead_.first);
        IRRender::destroyResource<Texture3D>(idTextureWrite_.first);
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

    /// Winning-light ID texture holding the latest propagated IDs, in
    /// lockstep with `getReadTexture()`. Sampled (NEAREST) by
    /// `LIGHTING_TO_TRIXEL` to recover which light lit each cell (#2318).
    Texture3D *getReadIdTexture() const {
        IR_ASSERT(
            idTextureRead_.second != nullptr,
            "C_CanvasLightVolume::getReadIdTexture() called on default-"
            "constructed instance."
        );
        return idTextureRead_.second;
    }

    /// Scratch ID texture written by the next propagate pass; swapped to
    /// the read side via `swap()` alongside the color pair.
    Texture3D *getWriteIdTexture() const {
        IR_ASSERT(
            idTextureWrite_.second != nullptr,
            "C_CanvasLightVolume::getWriteIdTexture() called on default-"
            "constructed instance."
        );
        return idTextureWrite_.second;
    }

    /// Promote `textureWrite_` to the read side after a propagation
    /// pass. Subsequent reads (`getReadTexture()`) see the new contents
    /// while the old read texture becomes the next pass's scratch. The ID
    /// pair swaps in lockstep so IDs never desync from their colors mid-
    /// chain (#2318).
    void swap() {
        std::swap(textureRead_, textureWrite_);
        std::swap(idTextureRead_, idTextureWrite_);
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
