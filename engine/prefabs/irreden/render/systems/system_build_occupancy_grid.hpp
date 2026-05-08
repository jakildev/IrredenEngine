#ifndef SYSTEM_BUILD_OCCUPANCY_GRID_H
#define SYSTEM_BUILD_OCCUPANCY_GRID_H

// Rebuilds the CPU-side 3D occupancy bitfield and uploads it to the SSBO
// at `kBufferIndex_OccupancyGrid` for downstream lighting compute shaders
// (COMPUTE_VOXEL_AO, COMPUTE_LIGHT_VOLUME).
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so lighting compute passes see the current frame's voxel state.
//
// The SSBO carries two parallel bitfields after the
// `OccupancyGridHeader`: the voxel bitfield (consumed by AO + light-volume
// propagation) and the **light-blocker bitfield** (consumed only by
// `c_propagate_light_volume`). The blocker bitfield rasterizes SDFs of
// `C_ShapeDescriptor + C_LightBlocker(blocksLOS_=true)` entities so they
// occlude point/spot light propagation, restoring the pre-#359 SDF-LOS
// behavior the GPU port lost. AO does not read the blocker bitfield, so
// SDF shapes do not start darkening AO unless their voxel-pool variant is
// also present.
//
// Phased-out producer: this system + the OccupancyGrid SSBO it feeds will
// be deleted in T-09Y once AO migrates to screen-space neighbour sampling
// (T-09X) and light-volume LOS moves to the GPU (T-072). The sun-shadow
// path no longer reads it — the screen-space sun-depth bake replaces the
// 3D occupancy march.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/detail/camera_anchor.hpp>
#include <irreden/math/sdf.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

/// Upper-bound grid edge length the SSBO is sized for — 256³ = 2 MB.
/// Per-instance `C_OccupancyGrid` components may be smaller, but the
/// SSBO is fixed so swapping between grid extents doesn't reallocate.
constexpr int kMaxOccupancyGridSideVoxels = 256;

/// Phase 1c (#360): the SSBO carries a 16-byte header (the camera-
/// anchored `worldOriginVoxel`) followed by two parallel bitfields — the
/// voxel occupancy grid (AO + light-volume LOS) followed by the
/// light-blocker grid (light-volume LOS only, populated from
/// `C_ShapeDescriptor + C_LightBlocker` entities). Both bitfields share
/// the same camera-anchored layout so a single world→local index
/// translation works for either lookup.
constexpr std::size_t kOccupancyHeaderByteSize = sizeof(OccupancyGridHeader);
constexpr std::size_t kOccupancyBitfieldByteSize =
    (static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels)) /
    8u;
constexpr std::size_t kOccupancyBitfieldUintCount = kOccupancyBitfieldByteSize / sizeof(std::uint32_t);
constexpr std::size_t kOccupancySSBOByteSize =
    kOccupancyHeaderByteSize + 2u * kOccupancyBitfieldByteSize;

/// Byte offset of the light-blocker bitfield region inside
/// `OccupancyGridBuffer`. The propagate shader indexes the same `uint[]`
/// as the voxel bitfield, just shifted by `kOccupancyBitfieldUintCount`.
constexpr std::size_t kLightBlockerBitfieldByteOffset =
    kOccupancyHeaderByteSize + kOccupancyBitfieldByteSize;

namespace detail {

/// Index helpers for the light-blocker bitfield. The bitfield mirrors the
/// 256³ voxel occupancy grid layout (camera-anchored, `[z][y][x]`
/// row-major, LSB = cell 0), so the translation matches `flatIndex` in
/// `C_OccupancyGrid` and `occupancyGetBit` in the propagate shader. Both
/// the CPU rasterizer here and the GPU lookup must use the same encoding
/// — see `c_propagate_light_volume.glsl::lightBlockerGetBit`.
inline bool blockerInBounds(int wx, int wy, int wz, const ivec3 &origin) {
    constexpr int kHalf = kMaxOccupancyGridSideVoxels / 2;
    const int lx = wx - origin.x;
    const int ly = wy - origin.y;
    const int lz = wz - origin.z;
    return lx >= -kHalf && lx < kHalf && ly >= -kHalf && ly < kHalf && lz >= -kHalf && lz < kHalf;
}

inline std::size_t blockerFlatIndex(int wx, int wy, int wz, const ivec3 &origin) {
    constexpr std::size_t kHalf = kMaxOccupancyGridSideVoxels / 2;
    constexpr std::size_t kSize = kMaxOccupancyGridSideVoxels;
    const std::size_t x = static_cast<std::size_t>(wx - origin.x) + kHalf;
    const std::size_t y = static_cast<std::size_t>(wy - origin.y) + kHalf;
    const std::size_t z = static_cast<std::size_t>(wz - origin.z) + kHalf;
    return (z * kSize + y) * kSize + x;
}

inline void blockerSetBit(
    std::vector<std::uint32_t> &bitfield,
    int wx,
    int wy,
    int wz,
    const ivec3 &origin
) {
    if (!blockerInBounds(wx, wy, wz, origin))
        return;
    const std::size_t flat = blockerFlatIndex(wx, wy, wz, origin);
    bitfield[flat >> 5u] |= (1u << (flat & 31u));
}

/// Rasterize one `C_ShapeDescriptor` into the light-blocker bitfield. The
/// AABB is clipped to the camera-anchored window, then each integer cell
/// inside the AABB is tested against the SDF. Cells with
/// `evaluate(...) <= kSurfaceThreshold` are interior surface samples and
/// get marked as blockers.
///
/// Cost is bounded by the shape's AABB volume (`(2h)^3` cells). Typical
/// blockers are small (a 6-voxel pillar = 216 evals; a 12-voxel wall =
/// 1728 evals); the per-shape budget stays well inside the BUILD_*
/// system's existing per-frame allocation.
inline void rasterizeShapeBlocker(
    const C_ShapeDescriptor &shape,
    const vec3 &shapeWorldPos,
    std::vector<std::uint32_t> &bitfield,
    const ivec3 &origin
) {
    const IRMath::SDF::ShapeType shapeType =
        static_cast<IRMath::SDF::ShapeType>(shape.shapeType_);
    const vec4 effectiveParams = IRMath::SDF::effectiveParams(shapeType, shape.params_);
    const vec3 boundingHalf = IRMath::SDF::boundingHalf(shapeType, effectiveParams);

    constexpr int kHalf = kMaxOccupancyGridSideVoxels / 2;
    const int gridXMin = origin.x - kHalf;
    const int gridXMax = origin.x + kHalf - 1;
    const int gridYMin = origin.y - kHalf;
    const int gridYMax = origin.y + kHalf - 1;
    const int gridZMin = origin.z - kHalf;
    const int gridZMax = origin.z + kHalf - 1;

    // World-space AABB of the shape, padded by 1 cell to cover surface
    // samples that round to a neighbor cell. Clipped to the grid window
    // so an off-camera shape contributes nothing without iterating
    // out-of-range cells.
    const int xMin = IRMath::max(static_cast<int>(IRMath::floor(shapeWorldPos.x - boundingHalf.x)) - 1, gridXMin);
    const int xMax = IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.x + boundingHalf.x)) + 1, gridXMax);
    const int yMin = IRMath::max(static_cast<int>(IRMath::floor(shapeWorldPos.y - boundingHalf.y)) - 1, gridYMin);
    const int yMax = IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.y + boundingHalf.y)) + 1, gridYMax);
    const int zMin = IRMath::max(static_cast<int>(IRMath::floor(shapeWorldPos.z - boundingHalf.z)) - 1, gridZMin);
    const int zMax = IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.z + boundingHalf.z)) + 1, gridZMax);

    if (xMin > xMax || yMin > yMax || zMin > zMax)
        return;

    for (int wz = zMin; wz <= zMax; ++wz) {
        for (int wy = yMin; wy <= yMax; ++wy) {
            for (int wx = xMin; wx <= xMax; ++wx) {
                const vec3 localPos =
                    vec3(static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz)) -
                    shapeWorldPos;
                if (IRMath::SDF::evaluate(localPos, shapeType, effectiveParams) <=
                    IRMath::SDF::kSurfaceThreshold) {
                    blockerSetBit(bitfield, wx, wy, wz, origin);
                }
            }
        }
    }
}

/// Iterate every `C_ShapeDescriptor + C_LightBlocker + C_PositionGlobal3D`
/// entity and rasterize the opt-in (`blocksLOS_=true`) blockers into the
/// supplied bitfield. Uses the batched-archetype-query pattern (no
/// per-entity getComponent in the tick). Returns the number of shapes
/// rasterized so the caller can skip the subData upload when no
/// blockers exist (the common case for demos that opt out at the
/// `C_LightBlocker{false, ...}` flag level).
inline std::size_t rasterizeAllBlockers(
    std::vector<std::uint32_t> &bitfield,
    const ivec3 &origin
) {
    const auto include = IREntity::getArchetype<
        C_ShapeDescriptor,
        C_LightBlocker,
        C_PositionGlobal3D>();
    const auto nodes = IREntity::queryArchetypeNodesSimple(include);
    std::size_t rasterized = 0;
    for (auto *node : nodes) {
        auto &shapes = IREntity::getComponentData<C_ShapeDescriptor>(node);
        auto &blockers = IREntity::getComponentData<C_LightBlocker>(node);
        auto &positions = IREntity::getComponentData<C_PositionGlobal3D>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (!blockers[i].blocksLOS_)
                continue;
            rasterizeShapeBlocker(shapes[i], positions[i].pos_, bitfield, origin);
            ++rasterized;
        }
    }
    return rasterized;
}

} // namespace detail

template <> struct System<BUILD_OCCUPANCY_GRID> {
    struct Params {
        Buffer *ssbo_ = nullptr;
        OccupancyGridHeader header_{};
        /// CPU mirror of the light-blocker bitfield. Allocated once at
        /// `create()` and reused every frame.
        std::vector<std::uint32_t> blockerBitfield_{};
        /// True when the previous frame uploaded a non-empty blocker
        /// bitfield. We need to do an explicit zero-upload one more
        /// time after the last blocker disappears, so the GPU side
        /// stops blocking light. After that, both CPU clear and GPU
        /// upload are skipped while the scene contains zero
        /// `C_LightBlocker(blocksLOS_=true)` shapes — the common case.
        bool prevFrameHadBlockers_ = false;
        /// Cached per-frame in the per-entity tick, consumed in endTick.
        /// Raw pointer into the occupancy grid's bitfield vector —
        /// valid for the duration of one pipeline-stage execution.
        ivec3 origin_{};
        const std::uint32_t *gridBitfieldData_ = nullptr;
        std::size_t gridBitfieldByteSize_ = 0;
    };

    static SystemId create() {
        IRRender::createNamedResource<Buffer>(
            "OccupancyGridBuffer",
            nullptr,
            kOccupancySSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyGrid
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->ssbo_ = IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        p->blockerBitfield_.assign(kOccupancyBitfieldUintCount, 0u);
        // The SSBO is created with `nullptr` initial data, so the
        // blocker region's contents are undefined until the system
        // first uploads. Push a zero-fill once at init so the
        // propagate shader's `lightBlockerGetBit` returns false on the
        // first frame even when no `C_LightBlocker` entities exist.
        p->ssbo_->subData(
            static_cast<std::ptrdiff_t>(kLightBlockerBitfieldByteOffset),
            p->blockerBitfield_.size() * sizeof(std::uint32_t),
            p->blockerBitfield_.data()
        );

        SystemId systemId = createSystem<C_VoxelPool, C_OccupancyGrid>(
            "BuildOccupancyGrid",
            [p](IREntity::EntityId, C_VoxelPool &pool, C_OccupancyGrid &grid) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
                IR_ASSERT(
                    grid.sizeVoxels() == kMaxOccupancyGridSideVoxels,
                    "Lighting occupancy shaders currently require a 256^3 grid"
                );

                // Phase 1c (#360): re-center on the iso camera each
                // frame. `setBit` / `inBounds` translate world→local
                // internally, so the populate path stays origin-
                // agnostic; only the GPU consumers need to know the
                // anchor (uploaded via the SSBO header below).
                grid.setWorldOriginVoxel(IRRender::detail::cameraAnchorVoxel());

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Clear", IR_PROFILER_COLOR_RENDER);
                    std::fill(grid.bitfield().begin(), grid.bitfield().end(), 0u);
                }

                const auto &globals = pool.getPositionGlobals();
                const auto &colors = pool.getColors();
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
                        if (!grid.inBounds(cell.x, cell.y, cell.z))
                            continue;
                        grid.setBit(cell.x, cell.y, cell.z);
                    }
                }

                p->origin_ = grid.worldOriginVoxel();
                p->gridBitfieldData_ = grid.bitfield().data();
                p->gridBitfieldByteSize_ = grid.bitfieldByteSize();
            },
            [p]() { p->ssbo_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid); },
            [p]() {
                if (p->gridBitfieldData_ == nullptr)
                    return;
                std::size_t blockerCount = 0;
                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::PopulateBlockers", IR_PROFILER_COLOR_RENDER);
                    // Skip the 2 MB memset + rasterize when we know the
                    // bitfield is already all-zero. After the last
                    // blocker disappears we still need one trailing
                    // clear+upload to drain stale bits from the GPU
                    // bitfield, but every subsequent frame can skip
                    // both the CPU fill and the GPU subData.
                    if (p->prevFrameHadBlockers_) {
                        std::fill(p->blockerBitfield_.begin(), p->blockerBitfield_.end(), 0u);
                    }
                    blockerCount = detail::rasterizeAllBlockers(p->blockerBitfield_, p->origin_);
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Upload", IR_PROFILER_COLOR_RENDER);
                    p->header_.worldOriginVoxel_ = ivec4(p->origin_.x, p->origin_.y, p->origin_.z, 0);
                    p->ssbo_->subData(0, kOccupancyHeaderByteSize, &p->header_);
                    p->ssbo_->subData(
                        static_cast<std::ptrdiff_t>(kOccupancyHeaderByteSize),
                        p->gridBitfieldByteSize_,
                        p->gridBitfieldData_
                    );
                    // Upload the blocker region only when there are
                    // blockers this frame, OR when there were blockers
                    // last frame (so the GPU's stale bits are zeroed
                    // exactly once on the transition to empty).
                    if (blockerCount > 0 || p->prevFrameHadBlockers_) {
                        p->ssbo_->subData(
                            static_cast<std::ptrdiff_t>(kLightBlockerBitfieldByteOffset),
                            p->blockerBitfield_.size() * sizeof(std::uint32_t),
                            p->blockerBitfield_.data()
                        );
                    }
                }
                p->prevFrameHadBlockers_ = (blockerCount > 0);
                p->gridBitfieldData_ = nullptr;
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        IRRender::tagGpuStage(systemId, "buildOccupancyGrid");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_OCCUPANCY_GRID_H */
