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
// v1 scope: EMISSIVE lights use 6-connected BFS while POINT lights use
// Euclidean falloff plus occupancy-grid LOS checks. SPOT shaping, dirty-
// tracked re-runs, and a GPU wavefront BFS are deferred to follow-up tasks.

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
    std::vector<std::uint8_t> &buffer, int wx, int wy, int wz, Color emit, float falloff
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
    // Alpha unused by the shader (samples .rgb only). Force 255 so the
    // RGBA8 upload doesn't ship uninitialised bytes through the driver.
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
    if (radius <= 0 || intensity <= 0.0f)
        return;
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

        const float falloff =
            std::max(0.0f, (1.0f - static_cast<float>(d) * invRadius) * intensity);
        if (falloff > 0.0f) {
            writeLightTexel(buffer, x, y, z, emit, falloff);
        }
        if (d >= radius)
            continue;

        for (int n = 0; n < 6; ++n) {
            const int nx = x + kDx[n];
            const int ny = y + kDy[n];
            const int nz = z + kDz[n];
            if (!C_CanvasLightVolume::inBounds(nx, ny, nz))
                continue;
            const std::uint64_t key = packLightVoxelKey(nx, ny, nz);
            if (!visited.insert(key).second)
                continue;
            // Solid voxels block further propagation, but the surface of
            // the first solid voxel hit must still receive the falloff
            // value so the lighting pass sees illumination on the visible
            // surface (the trixel pixel's pos3D recovers the solid voxel,
            // not the empty cell next to it).
            const float nFalloff =
                std::max(0.0f, (1.0f - static_cast<float>(d + 1) * invRadius) * intensity);
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

inline bool hasLineOfSight(const C_OccupancyGrid &grid, ivec3 originVoxel, ivec3 targetVoxel) {
    const ivec3 delta = targetVoxel - originVoxel;
    const int steps = std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)});
    if (steps <= 1)
        return true;

    const vec3 origin = vec3(originVoxel);
    const vec3 step = vec3(delta) / static_cast<float>(steps);
    for (int i = 1; i < steps; ++i) {
        const vec3 p = origin + step * static_cast<float>(i);
        const ivec3 cell = ivec3(glm::round(p));
        if (cell == originVoxel || cell == targetVoxel)
            continue;
        if (grid.getBit(cell.x, cell.y, cell.z))
            return false;
    }
    return true;
}

inline void fillPointLight(
    const C_OccupancyGrid &grid,
    std::vector<std::uint8_t> &buffer,
    ivec3 originVoxel,
    Color emit,
    float intensity,
    int radius
) {
    radius = std::clamp(radius, 0, 32);
    if (radius <= 0 || intensity <= 0.0f)
        return;
    if (!C_CanvasLightVolume::inBounds(originVoxel.x, originVoxel.y, originVoxel.z)) {
        return;
    }

    const float invRadius = 1.0f / static_cast<float>(radius);
    const int xMin = std::max(originVoxel.x - radius, -kLightVolumeHalfExtent);
    const int xMax = std::min(originVoxel.x + radius, kLightVolumeHalfExtent - 1);
    const int yMin = std::max(originVoxel.y - radius, -kLightVolumeHalfExtent);
    const int yMax = std::min(originVoxel.y + radius, kLightVolumeHalfExtent - 1);
    const int zMin = std::max(originVoxel.z - radius, -kLightVolumeHalfExtent);
    const int zMax = std::min(originVoxel.z + radius, kLightVolumeHalfExtent - 1);

    for (int z = zMin; z <= zMax; ++z) {
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const vec3 delta = vec3(x, y, z) - vec3(originVoxel);
                const float distance = glm::length(delta);
                if (distance > static_cast<float>(radius))
                    continue;

                const ivec3 target{x, y, z};
                if (!hasLineOfSight(grid, originVoxel, target))
                    continue;

                const float falloff = (1.0f - distance * invRadius) * intensity;
                if (falloff > 0.0f) {
                    writeLightTexel(buffer, x, y, z, emit, falloff);
                }
            }
        }
    }
}

inline bool
isInsideSpotCone(ivec3 originVoxel, ivec3 targetVoxel, vec3 direction, float coneAngleDeg) {
    const vec3 toTarget = vec3(targetVoxel - originVoxel);
    const float distance = glm::length(toTarget);
    if (distance <= 0.0f)
        return true;

    const float directionLength = glm::length(direction);
    if (directionLength <= 0.0f)
        return false;

    const vec3 coneDir = direction / directionLength;
    const float halfAngleDeg = std::clamp(coneAngleDeg, 0.0f, 180.0f) * 0.5f;
    const float radians = halfAngleDeg * 0.01745329251994329577f;
    const float minDot = std::cos(radians);
    return glm::dot(toTarget / distance, coneDir) >= minDot;
}

inline void fillSpotLight(
    const C_OccupancyGrid &grid,
    std::vector<std::uint8_t> &buffer,
    ivec3 originVoxel,
    Color emit,
    float intensity,
    int radius,
    vec3 direction,
    float coneAngleDeg
) {
    radius = std::clamp(radius, 0, 32);
    if (radius <= 0 || intensity <= 0.0f)
        return;
    if (!C_CanvasLightVolume::inBounds(originVoxel.x, originVoxel.y, originVoxel.z)) {
        return;
    }

    const float invRadius = 1.0f / static_cast<float>(radius);
    const int xMin = std::max(originVoxel.x - radius, -kLightVolumeHalfExtent);
    const int xMax = std::min(originVoxel.x + radius, kLightVolumeHalfExtent - 1);
    const int yMin = std::max(originVoxel.y - radius, -kLightVolumeHalfExtent);
    const int yMax = std::min(originVoxel.y + radius, kLightVolumeHalfExtent - 1);
    const int zMin = std::max(originVoxel.z - radius, -kLightVolumeHalfExtent);
    const int zMax = std::min(originVoxel.z + radius, kLightVolumeHalfExtent - 1);

    for (int z = zMin; z <= zMax; ++z) {
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const ivec3 target{x, y, z};
                if (!isInsideSpotCone(originVoxel, target, direction, coneAngleDeg)) {
                    continue;
                }

                const vec3 delta = vec3(target - originVoxel);
                const float distance = glm::length(delta);
                if (distance > static_cast<float>(radius))
                    continue;
                if (!hasLineOfSight(grid, originVoxel, target))
                    continue;

                const float falloff = (1.0f - distance * invRadius) * intensity;
                if (falloff > 0.0f) {
                    writeLightTexel(buffer, x, y, z, emit, falloff);
                }
            }
        }
    }
}

template <typename Function> void forEachLightSourceWithPosition(Function &&function) {
    const auto include = IREntity::getArchetype<C_LightSource, C_PositionGlobal3D>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);
    for (auto *node : nodes) {
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        auto &positions = IREntity::getComponentData<C_PositionGlobal3D>(node);
        for (int i = 0; i < node->length_; ++i) {
            function(lights[i], positions[i]);
        }
    }
}

inline ivec3 roundedLightOrigin(const C_PositionGlobal3D &position) {
    return ivec3(
        static_cast<int>(std::lround(position.pos_.x)),
        static_cast<int>(std::lround(position.pos_.y)),
        static_cast<int>(std::lround(position.pos_.z))
    );
}

} // namespace detail

template <> struct System<COMPUTE_LIGHT_VOLUME> {
    static SystemId create() {
        return createSystem<C_OccupancyGrid, C_CanvasLightVolume, C_TrixelCanvasRenderBehavior>(
            "ComputeLightVolume",
            [](C_OccupancyGrid &grid,
               C_CanvasLightVolume &volume,
               const C_TrixelCanvasRenderBehavior &behavior) {
                if (!behavior.useCameraPositionIso_)
                    return;
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                {
                    IR_PROFILE_BLOCK("ComputeLightVolume::Clear", IR_PROFILER_COLOR_RENDER);
                    std::fill(volume.cpuBuffer_.begin(), volume.cpuBuffer_.end(), std::uint8_t{0});
                }

                {
                    IR_PROFILE_BLOCK("ComputeLightVolume::Populate", IR_PROFILER_COLOR_RENDER);
                    detail::forEachLightSourceWithPosition(
                        [&grid, &volume](C_LightSource &light, const C_PositionGlobal3D &position) {
                            const ivec3 originVoxel = detail::roundedLightOrigin(position);
                            switch (light.type_) {
                            case LightType::EMISSIVE:
                                detail::floodFillEmissive(
                                    grid,
                                    volume.cpuBuffer_,
                                    originVoxel,
                                    light.emitColor_,
                                    light.intensity_,
                                    static_cast<int>(light.radius_)
                                );
                                break;
                            case LightType::POINT:
                                detail::fillPointLight(
                                    grid,
                                    volume.cpuBuffer_,
                                    originVoxel,
                                    light.emitColor_,
                                    light.intensity_,
                                    static_cast<int>(light.radius_)
                                );
                                break;
                            case LightType::SPOT:
                                detail::fillSpotLight(
                                    grid,
                                    volume.cpuBuffer_,
                                    originVoxel,
                                    light.emitColor_,
                                    light.intensity_,
                                    static_cast<int>(light.radius_),
                                    light.direction_,
                                    light.coneAngleDeg_
                                );
                                break;
                            default:
                                break;
                            }
                        }
                    );
                }

                {
                    IR_PROFILE_BLOCK("ComputeLightVolume::Upload", IR_PROFILER_COLOR_RENDER);
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
