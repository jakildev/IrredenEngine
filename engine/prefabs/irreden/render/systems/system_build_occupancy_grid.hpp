#ifndef SYSTEM_BUILD_OCCUPANCY_GRID_H
#define SYSTEM_BUILD_OCCUPANCY_GRID_H

// Rebuilds the CPU-side 3D occupancy bitfield and uploads it to the SSBO
// at `kBufferIndex_OccupancyGrid` for downstream lighting compute shaders.
// Also rebuilds a small per-entity bbox table (one entry per voxel-pool
// entity) and uploads it to `kBufferIndex_OccupancyEntityBounds` so the
// sun-shadow compute can skip self-cells during the occupancy march
// (parity with the analytic path's selfEntityId exclusion).
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so lighting compute passes see the current frame's voxel state.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

/// Upper-bound grid edge length the SSBO is sized for — 256³ = 2 MB.
/// Per-instance `C_OccupancyGrid` components may be smaller, but the
/// SSBO is fixed so swapping between grid extents doesn't reallocate.
constexpr int kMaxOccupancyGridSideVoxels = 256;

constexpr std::size_t kOccupancySSBOByteSize =
    (static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels)) /
    8u;

/// Upper bound on simultaneously-rendered voxel-pool entities the shadow
/// shader will linear-scan for self-bbox lookup. Sized for typical demo
/// scenes; bump if a creation needs more and accept the per-pixel cost.
constexpr int kMaxOccupancyEntityBounds = 256;
constexpr std::size_t kOccupancyBoundsSSBOByteSize =
    static_cast<std::size_t>(kMaxOccupancyEntityBounds) * sizeof(GPUOccupancyEntityBounds);

namespace detail {

/// How many entries `system_build_occupancy_grid` uploaded to the bounds
/// SSBO this frame. Read by `system_compute_sun_shadow` to populate
/// `FrameDataSun::occupancyBoundsCount_`. Both systems are part of the
/// RENDER pipeline; build runs before shadow each frame, so the value is
/// always fresh by the time shadow reads it.
inline int &occupancyEntityBoundsCount() {
    static int s_count = 0;
    return s_count;
}

} // namespace detail

template <> struct System<BUILD_OCCUPANCY_GRID> {
    static SystemId create() {
        IRRender::createNamedResource<Buffer>(
            "OccupancyGridBuffer",
            nullptr,
            kOccupancySSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyGrid
        );
        IRRender::createNamedResource<Buffer>(
            "OccupancyEntityBoundsBuffer",
            nullptr,
            kOccupancyBoundsSSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyEntityBounds
        );

        static Buffer *s_ssbo = IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        static Buffer *s_boundsSSBO =
            IRRender::getNamedResource<Buffer>("OccupancyEntityBoundsBuffer");

        // Scratch reused across frames. Per-entity bbox accumulator keyed by
        // entity id; cleared at the start of each tick. Vector for upload is
        // packed each frame so the shader sees a contiguous, valid prefix.
        static std::unordered_map<IREntity::EntityId, GPUOccupancyEntityBounds> s_boundsByEntity;
        static std::vector<GPUOccupancyEntityBounds> s_boundsUpload;

        return createSystem<C_VoxelPool, C_OccupancyGrid>(
            "BuildOccupancyGrid",
            [](IREntity::EntityId, C_VoxelPool &pool, C_OccupancyGrid &grid) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
                IR_ASSERT(
                    grid.sizeVoxels() == kMaxOccupancyGridSideVoxels,
                    "Lighting occupancy shaders currently require a 256^3 grid"
                );

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Clear", IR_PROFILER_COLOR_RENDER);
                    std::fill(grid.bitfield().begin(), grid.bitfield().end(), 0u);
                }
                s_boundsByEntity.clear();

                const auto &globals = pool.getPositionGlobals();
                const auto &colors = pool.getColors();
                const auto &voxelEntities = pool.getEntityIds();
                const int liveCount = pool.getLiveVoxelCount();

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Populate", IR_PROFILER_COLOR_RENDER);
                    for (int i = 0; i < liveCount; ++i) {
                        if (colors[i].color_.alpha_ == 0)
                            continue;
                        const vec3 &wp = globals[i].pos_;
                        // Round-half-up — must match the `roundHalfUp(...)`
                        // helper in shaders/ir_iso_common.glsl + .metal. See
                        // IRMath::roundHalfUp doc-comment for why this rule
                        // (vs std::lround / glm::round) is required for the
                        // CPU↔GPU occupancy-grid handshake.
                        const ivec3 cell = IRMath::roundVec3HalfUp(wp);
                        const int wx = cell.x;
                        const int wy = cell.y;
                        const int wz = cell.z;
                        if (!grid.inBounds(wx, wy, wz))
                            continue;
                        grid.setBit(wx, wy, wz);

                        const IREntity::EntityId owner = voxelEntities[i];
                        if (owner == IREntity::kNullEntity)
                            continue;

                        auto [it, inserted] = s_boundsByEntity.try_emplace(owner);
                        GPUOccupancyEntityBounds &b = it->second;
                        if (inserted) {
                            b.entityId = uvec4(static_cast<std::uint32_t>(owner), 0u, 0u, 0u);
                            b.minCell = ivec4(wx, wy, wz, 0);
                            b.maxCell = ivec4(wx, wy, wz, 0);
                        } else {
                            b.minCell.x = IRMath::min(b.minCell.x, wx);
                            b.minCell.y = IRMath::min(b.minCell.y, wy);
                            b.minCell.z = IRMath::min(b.minCell.z, wz);
                            b.maxCell.x = IRMath::max(b.maxCell.x, wx);
                            b.maxCell.y = IRMath::max(b.maxCell.y, wy);
                            b.maxCell.z = IRMath::max(b.maxCell.z, wz);
                        }
                    }
                }

                {
                    // Rasterize SDF shape entities into the same occupancy
                    // grid that voxel-pool entities populate, so AO and the
                    // occupancy DDA in COMPUTE_SUN_SHADOW treat both kinds
                    // of geometry uniformly. Without this, only voxel-pool
                    // entities cast AO crease darkening on the floor and
                    // adjacent surfaces — SDF cubes / spheres / cones look
                    // like they're floating, breaking visual parity.
                    //
                    // Iterates `C_ShapeDescriptor + C_PositionGlobal3D`
                    // entities once per BUILD_OCCUPANCY_GRID tick, filters
                    // by visibility / alpha / `C_LightBlocker.castsShadow_`
                    // (mirrors `shapeCastsSunShadowAnalyticShadow`), and
                    // fills cells whose centers evaluate inside the SDF.
                    // Cost is O(sum_shapes(bbox volume)); typical demo
                    // scenes touch <10K cells per frame which is dominated
                    // by the upload below.
                    IR_PROFILE_BLOCK(
                        "BuildOccupancyGrid::PopulateSDFs", IR_PROFILER_COLOR_RENDER
                    );
                    const auto include =
                        IREntity::getArchetype<C_ShapeDescriptor, C_PositionGlobal3D>();
                    const IREntity::ComponentId blockerType =
                        IREntity::getComponentType<C_LightBlocker>();
                    auto sdfNodes = IREntity::queryArchetypeNodesSimple(include);

                    for (auto *node : sdfNodes) {
                        auto &shapes = IREntity::getComponentData<C_ShapeDescriptor>(node);
                        auto &positions =
                            IREntity::getComponentData<C_PositionGlobal3D>(node);
                        const bool hasBlocker = node->type_.contains(blockerType);
                        std::vector<C_LightBlocker> *blockers = nullptr;
                        if (hasBlocker) {
                            blockers = &IREntity::getComponentData<C_LightBlocker>(node);
                        }

                        for (int i = 0; i < node->length_; ++i) {
                            const C_ShapeDescriptor &shape = shapes[i];
                            // Same filter as the analytic shadow caster
                            // collection so the two code paths agree on
                            // which SDFs occlude.
                            if (shape.shapeType_ == IRRender::ShapeType::CUSTOM_SDF)
                                continue;
                            if ((shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE) == 0u)
                                continue;
                            if (shape.color_.alpha_ == 0)
                                continue;
                            if (blockers != nullptr && !(*blockers)[i].castsShadow_)
                                continue;

                            const auto sdfType =
                                static_cast<IRMath::SDF::ShapeType>(shape.shapeType_);
                            const vec4 effective =
                                IRMath::SDF::effectiveParams(sdfType, shape.params_);
                            const vec3 halfBounds =
                                IRMath::SDF::boundingHalf(sdfType, effective);
                            const vec3 &center = positions[i].pos_;
                            const ivec3 minCell =
                                IRMath::roundVec3HalfUp(center - halfBounds);
                            const ivec3 maxCell =
                                IRMath::roundVec3HalfUp(center + halfBounds);

                            const IREntity::EntityId owner = node->entities_[i];
                            bool hasAnyCell = false;
                            ivec3 entityMin{0};
                            ivec3 entityMax{0};

                            for (int z = minCell.z; z <= maxCell.z; ++z) {
                                for (int y = minCell.y; y <= maxCell.y; ++y) {
                                    for (int x = minCell.x; x <= maxCell.x; ++x) {
                                        if (!grid.inBounds(x, y, z))
                                            continue;
                                        const vec3 cellCenter =
                                            vec3(static_cast<float>(x),
                                                 static_cast<float>(y),
                                                 static_cast<float>(z));
                                        const float distance = IRMath::SDF::evaluate(
                                            cellCenter - center, sdfType, effective
                                        );
                                        if (distance > 0.0f)
                                            continue;
                                        grid.setBit(x, y, z);
                                        if (!hasAnyCell) {
                                            hasAnyCell = true;
                                            entityMin = ivec3(x, y, z);
                                            entityMax = ivec3(x, y, z);
                                        } else {
                                            entityMin.x = IRMath::min(entityMin.x, x);
                                            entityMin.y = IRMath::min(entityMin.y, y);
                                            entityMin.z = IRMath::min(entityMin.z, z);
                                            entityMax.x = IRMath::max(entityMax.x, x);
                                            entityMax.y = IRMath::max(entityMax.y, y);
                                            entityMax.z = IRMath::max(entityMax.z, z);
                                        }
                                    }
                                }
                            }

                            if (!hasAnyCell || owner == IREntity::kNullEntity)
                                continue;

                            auto [it, inserted] = s_boundsByEntity.try_emplace(owner);
                            GPUOccupancyEntityBounds &b = it->second;
                            if (inserted) {
                                b.entityId =
                                    uvec4(static_cast<std::uint32_t>(owner), 0u, 0u, 0u);
                                b.minCell = ivec4(entityMin, 0);
                                b.maxCell = ivec4(entityMax, 0);
                            } else {
                                b.minCell.x = IRMath::min(b.minCell.x, entityMin.x);
                                b.minCell.y = IRMath::min(b.minCell.y, entityMin.y);
                                b.minCell.z = IRMath::min(b.minCell.z, entityMin.z);
                                b.maxCell.x = IRMath::max(b.maxCell.x, entityMax.x);
                                b.maxCell.y = IRMath::max(b.maxCell.y, entityMax.y);
                                b.maxCell.z = IRMath::max(b.maxCell.z, entityMax.z);
                            }
                        }
                    }
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Upload", IR_PROFILER_COLOR_RENDER);
                    s_ssbo->subData(0, grid.bitfieldByteSize(), grid.bitfield().data());
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::UploadBounds", IR_PROFILER_COLOR_RENDER);
                    s_boundsUpload.clear();
                    s_boundsUpload.reserve(s_boundsByEntity.size());
                    for (auto &kv : s_boundsByEntity) {
                        if (static_cast<int>(s_boundsUpload.size()) >= kMaxOccupancyEntityBounds) {
                            IR_ASSERT(
                                false,
                                "Too many voxel-pool entities for OccupancyEntityBounds buffer"
                            );
                            break;
                        }
                        s_boundsUpload.push_back(kv.second);
                    }
                    if (!s_boundsUpload.empty()) {
                        s_boundsSSBO->subData(
                            0,
                            s_boundsUpload.size() * sizeof(GPUOccupancyEntityBounds),
                            s_boundsUpload.data()
                        );
                    }
                    detail::occupancyEntityBoundsCount() = static_cast<int>(s_boundsUpload.size());
                }
            },
            []() {
                s_ssbo->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid);
                s_boundsSSBO->bindBase(
                    BufferTarget::SHADER_STORAGE,
                    kBufferIndex_OccupancyEntityBounds
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_OCCUPANCY_GRID_H */
