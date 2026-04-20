#ifndef COMPONENT_CANVAS_LIGHT_VOLUME_H
#define COMPONENT_CANVAS_LIGHT_VOLUME_H

// World-space 3D light volume populated by COMPUTE_LIGHT_VOLUME (CPU
// flood-fill BFS) and consumed by LIGHTING_TO_TRIXEL. Opt-in: attach
// alongside C_TriangleCanvasTextures + C_OccupancyGrid to enable
// emissive flood-fill lighting for a canvas.
//
// The volume is centered at world origin and covers
// `[-kLightVolumeHalfExtent, +kLightVolumeHalfExtent)` voxels per axis
// at 1:1 voxel-per-texel resolution. Geometry outside this range is
// sampled with CLAMP_TO_EDGE — light contributions outside the box read
// zero.
//
// Sized smaller than `C_OccupancyGrid` (256³) on purpose: 8 MiB
// CPU+GPU keeps the per-frame upload bandwidth bounded for a v1 with
// no incremental updates. Future versions can scale up via dirty-region
// uploads or a sparse representation.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <cstdint>
#include <vector>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

constexpr int kLightVolumeSize = 128;
constexpr int kLightVolumeHalfExtent = kLightVolumeSize / 2;

struct C_CanvasLightVolume {
    std::pair<ResourceId, Texture3D *> texture_;
    /// Backing store for the BFS pass. Sized to the full 128³ × RGBA8
    /// volume (8 MiB) so the system can `subImage3D` the entire texture
    /// in one call. Held on the component (rather than re-allocated per
    /// frame) so the dirty-tracking path stays allocation-free.
    std::vector<std::uint8_t> cpuBuffer_;

    C_CanvasLightVolume()
        : texture_{IRRender::createResource<IRRender::Texture3D>(
              TextureKind::TEXTURE_3D,
              kLightVolumeSize,
              kLightVolumeSize,
              kLightVolumeSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , cpuBuffer_(
              static_cast<std::size_t>(kLightVolumeSize) *
                  static_cast<std::size_t>(kLightVolumeSize) *
                  static_cast<std::size_t>(kLightVolumeSize) * 4u,
              0u
          ) {}

    void onDestroy() {
        IRRender::destroyResource<Texture3D>(texture_.first);
    }

    Texture3D *getTexture() const {
        IR_ASSERT(
            texture_.second != nullptr,
            "C_CanvasLightVolume::getTexture() called on default-"
            "constructed instance — must be constructed via the "
            "default ctor (which allocates the GPU texture)."
        );
        return texture_.second;
    }

    static bool inBounds(int wx, int wy, int wz) {
        return wx >= -kLightVolumeHalfExtent && wx < kLightVolumeHalfExtent &&
               wy >= -kLightVolumeHalfExtent && wy < kLightVolumeHalfExtent &&
               wz >= -kLightVolumeHalfExtent && wz < kLightVolumeHalfExtent;
    }

    static std::size_t flatIndex(int wx, int wy, int wz) {
        const std::size_t x = static_cast<std::size_t>(wx + kLightVolumeHalfExtent);
        const std::size_t y = static_cast<std::size_t>(wy + kLightVolumeHalfExtent);
        const std::size_t z = static_cast<std::size_t>(wz + kLightVolumeHalfExtent);
        const std::size_t s = static_cast<std::size_t>(kLightVolumeSize);
        return ((z * s) + y) * s + x;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_LIGHT_VOLUME_H */
