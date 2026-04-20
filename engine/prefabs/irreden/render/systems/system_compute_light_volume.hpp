#ifndef SYSTEM_COMPUTE_LIGHT_VOLUME_H
#define SYSTEM_COMPUTE_LIGHT_VOLUME_H

// CPU-side flood-fill light propagation. For each EMISSIVE
// `C_LightSource` entity in the world, runs a 6-connected BFS through
// the canvas's `C_OccupancyGrid` (light is blocked by solid voxels)
// and max-composites the resulting RGB falloff into the canvas's
// `C_CanvasLightVolume` 3D texture. The texture is then sampled
// per-pixel by `LIGHTING_TO_TRIXEL`.
//
// Pipeline order constraint: must run after `BUILD_OCCUPANCY_GRID`
// (so the SSBO mirrors the current frame's voxel state) and before
// `LIGHTING_TO_TRIXEL` (which samples the light volume).
//
// v1 scope: only EMISSIVE lights are processed. Skylight (DIRECTIONAL),
// point-radiance shaping (POINT/SPOT), incremental updates, and a GPU
// wavefront BFS are deferred to follow-up tasks. The pass re-runs every
// frame — bounded by `(#emissive lights) × radius³` BFS visits, cheap
// for the v1 demo (one light, radius ≤ 32). Dirty-tracked re-runs are
// a follow-up: `BUILD_OCCUPANCY_GRID` clears the grid's dirty flag
// before this pass sees it, so a separate flag on either the grid or
// the light volume is required.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <tuple>
#include <unordered_set>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

namespace detail {

inline std::uint64_t packLightVoxelKey(int wx, int wy, int wz) {
    const std::uint64_t x = static_cast<std::uint64_t>(wx + kLightVolumeHalfExtent);
    const std::uint64_t y = static_cast<std::uint64_t>(wy + kLightVolumeHalfExtent);
    const std::uint64_t z = static_cast<std::uint64_t>(wz + kLightVolumeHalfExtent);
    return (z << 20) | (y << 10) | x;
}

inline void writeLightTexel(
    std::vector<std::uint8_t> &buffer,
    int wx,
    int wy,
    int wz,
    Color emit,
    float falloff
) {
    const std::size_t idx = C_CanvasLightVolume::flatIndex(wx, wy, wz) * 4u;
    const float r = static_cast<float>(emit.red_) * falloff;
    const float g = static_cast<float>(emit.green_) * falloff;
    const float b = static_cast<float>(emit.blue_) * falloff;
    const auto clamp8 = [](float v) {
        return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f));
    };
    // Max-composite so multiple lights overlap by max-channel rather
    // than additive (prevents oversaturation in shared regions).
    buffer[idx + 0] = std::max(buffer[idx + 0], clamp8(r));
    buffer[idx + 1] = std::max(buffer[idx + 1], clamp8(g));
    buffer[idx + 2] = std::max(buffer[idx + 2], clamp8(b));
    buffer[idx + 3] = 255u;
}

inline void floodFillEmissive(
    const C_OccupancyGrid &grid,
    std::vector<std::uint8_t> &buffer,
    ivec3 originVoxel,
    Color emit,
    float intensity,
    int radius
) {
    if (radius <= 0 || intensity <= 0.0f) return;
    if (!C_CanvasLightVolume::inBounds(originVoxel.x, originVoxel.y, originVoxel.z)) {
        return;
    }

    std::queue<std::tuple<int, int, int, int>> q;
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(static_cast<std::size_t>(radius) * radius * radius);
    q.emplace(originVoxel.x, originVoxel.y, originVoxel.z, 0);
    visited.insert(packLightVoxelKey(originVoxel.x, originVoxel.y, originVoxel.z));

    static constexpr int kDx[6] = {1, -1, 0, 0, 0, 0};
    static constexpr int kDy[6] = {0, 0, 1, -1, 0, 0};
    static constexpr int kDz[6] = {0, 0, 0, 0, 1, -1};
    const float invRadius = 1.0f / static_cast<float>(radius);

    while (!q.empty()) {
        const auto [x, y, z, d] = q.front();
        q.pop();

        const float falloff = std::max(
            0.0f,
            (1.0f - static_cast<float>(d) * invRadius) * intensity
        );
        if (falloff > 0.0f) {
            writeLightTexel(buffer, x, y, z, emit, falloff);
        }
        if (d >= radius) continue;

        for (int n = 0; n < 6; ++n) {
            const int nx = x + kDx[n];
            const int ny = y + kDy[n];
            const int nz = z + kDz[n];
            if (!C_CanvasLightVolume::inBounds(nx, ny, nz)) continue;
            const std::uint64_t key = packLightVoxelKey(nx, ny, nz);
            if (!visited.insert(key).second) continue;
            // Solid voxels block further propagation, but the surface of
            // the first solid voxel hit must still receive the falloff
            // value so the lighting pass sees illumination on the visible
            // surface (the trixel pixel's pos3D recovers the solid voxel,
            // not the empty cell next to it).
            const float nFalloff = std::max(
                0.0f,
                (1.0f - static_cast<float>(d + 1) * invRadius) * intensity
            );
            if (grid.getBit(nx, ny, nz)) {
                if (nFalloff > 0.0f) {
                    writeLightTexel(buffer, nx, ny, nz, emit, nFalloff);
                }
                continue;
            }
            q.emplace(nx, ny, nz, d + 1);
        }
    }
}

} // namespace detail

template <> struct System<COMPUTE_LIGHT_VOLUME> {
    static SystemId create() {
        return createSystem<
            C_OccupancyGrid,
            C_CanvasLightVolume,
            C_TrixelCanvasRenderBehavior
        >(
            "ComputeLightVolume",
            [](C_OccupancyGrid &grid,
               C_CanvasLightVolume &volume,
               const C_TrixelCanvasRenderBehavior &behavior) {
                if (!behavior.useCameraPositionIso_) return;
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                {
                    IR_PROFILE_BLOCK(
                        "ComputeLightVolume::Clear",
                        IR_PROFILER_COLOR_RENDER
                    );
                    std::fill(
                        volume.cpuBuffer_.begin(),
                        volume.cpuBuffer_.end(),
                        std::uint8_t{0}
                    );
                }

                {
                    IR_PROFILE_BLOCK(
                        "ComputeLightVolume::BFS",
                        IR_PROFILER_COLOR_RENDER
                    );
                    IREntity::forEachComponent<C_LightSource>(
                        [&grid, &volume](
                            EntityId id,
                            C_LightSource &light
                        ) {
                            if (light.type_ != LightType::EMISSIVE) return;
                            const auto &pos =
                                IREntity::getComponent<C_PositionGlobal3D>(id);
                            const ivec3 originVoxel(
                                static_cast<int>(std::lround(pos.pos_.x)),
                                static_cast<int>(std::lround(pos.pos_.y)),
                                static_cast<int>(std::lround(pos.pos_.z))
                            );
                            detail::floodFillEmissive(
                                grid,
                                volume.cpuBuffer_,
                                originVoxel,
                                light.emitColor_,
                                light.intensity_,
                                static_cast<int>(light.radius_)
                            );
                        }
                    );
                }

                {
                    IR_PROFILE_BLOCK(
                        "ComputeLightVolume::Upload",
                        IR_PROFILER_COLOR_RENDER
                    );
                    volume.getTexture()->subImage3D(
                        kLightVolumeSize,
                        kLightVolumeSize,
                        kLightVolumeSize,
                        PixelDataFormat::RGBA,
                        PixelDataType::UNSIGNED_BYTE,
                        volume.cpuBuffer_.data()
                    );
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_LIGHT_VOLUME_H */
