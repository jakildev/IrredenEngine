#ifndef SYSTEM_BUILD_LIGHT_OCCLUSION_GRID_H
#define SYSTEM_BUILD_LIGHT_OCCLUSION_GRID_H

// Rebuilds the camera-anchored 3D light-occlusion bitfield and uploads
// it to the SSBO at `kBufferIndex_LightOcclusionGrid` for the
// light-volume propagation compute shader (`COMPUTE_LIGHT_VOLUME`).
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so light-volume propagation sees the current frame's voxel state.
//
// The SSBO carries two parallel bitfields after the
// `LightOcclusionGridHeader`: the voxel bitfield (consumed by
// `c_propagate_light_volume`) and the **light-blocker bitfield**
// (consumed only by `c_propagate_light_volume`). The blocker bitfield
// rasterizes SDFs of `C_ShapeDescriptor + C_LightBlocker(blocksLOS_=true)`
// entities so they occlude point/spot light propagation, restoring the
// pre-#359 SDF-LOS behavior the GPU port lost. AO does not read either
// bitfield (it migrated to screen-space neighbour sampling in T-091).
//
// The system selects the main rendering canvas via
// `<C_VoxelPool, C_TrixelCanvasRenderBehavior>` and the
// `useCameraPositionIso_` flag — same gate `COMPUTE_LIGHT_VOLUME` uses.
// Bitfield storage lives in `SystemParams` (no per-canvas component opt-in).
//
// Phased-out producer: this system + the LightOcclusionGrid SSBO it
// feeds are scheduled for full removal in T-09Y once light-volume LOS
// moves off the world-space bitfield.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
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

/// Edge length of the camera-anchored light-occlusion grid the SSBO is
/// sized for — 256³ = 2 MB voxel bitfield + 2 MB blocker bitfield.
constexpr int kMaxLightOcclusionGridSideVoxels = 256;

/// SSBO layout (Phase 1c / #360): a 16-byte header carrying the camera-
/// anchored `worldOriginVoxel` followed by two parallel bitfields — the
/// voxel-existence grid (consumed by `c_propagate_light_volume`)
/// followed by the light-blocker grid (also consumed only by
/// `c_propagate_light_volume`, populated from `C_ShapeDescriptor +
/// C_LightBlocker` entities). Both bitfields share the same camera-
/// anchored layout so a single world→local index translation works for
/// either lookup.
constexpr std::size_t kLightOcclusionHeaderByteSize = sizeof(LightOcclusionGridHeader);
constexpr std::size_t kLightOcclusionBitfieldByteSize =
    (static_cast<std::size_t>(kMaxLightOcclusionGridSideVoxels) *
     static_cast<std::size_t>(kMaxLightOcclusionGridSideVoxels) *
     static_cast<std::size_t>(kMaxLightOcclusionGridSideVoxels)) /
    8u;
constexpr std::size_t kLightOcclusionBitfieldUintCount =
    kLightOcclusionBitfieldByteSize / sizeof(std::uint32_t);
constexpr std::size_t kLightOcclusionSSBOByteSize =
    kLightOcclusionHeaderByteSize + 2u * kLightOcclusionBitfieldByteSize;

/// Byte offset of the light-blocker bitfield region inside
/// `LightOcclusionGridBuffer`. The propagate shader indexes the same
/// `uint[]` as the voxel bitfield, just shifted by
/// `kLightOcclusionBitfieldUintCount`.
constexpr std::size_t kLightBlockerBitfieldByteOffset =
    kLightOcclusionHeaderByteSize + kLightOcclusionBitfieldByteSize;

namespace detail {

/// Index helpers for both bitfields (voxel existence + light blockers).
/// The two bitfields mirror the same 256³ camera-anchored layout —
/// `[z][y][x]` row-major, LSB = cell 0 — so the math is identical. Both
/// the CPU rasterizer here and the GPU lookup must use the same
/// encoding — see `c_propagate_light_volume.glsl::voxelOcclusionGetBit`
/// and `lightBlockerGetBit`.
inline bool gridInBounds(int wx, int wy, int wz, const ivec3 &origin) {
    constexpr int kHalf = kMaxLightOcclusionGridSideVoxels / 2;
    const int lx = wx - origin.x;
    const int ly = wy - origin.y;
    const int lz = wz - origin.z;
    return lx >= -kHalf && lx < kHalf && ly >= -kHalf && ly < kHalf && lz >= -kHalf && lz < kHalf;
}

inline std::size_t gridFlatIndex(int wx, int wy, int wz, const ivec3 &origin) {
    constexpr std::size_t kHalf = kMaxLightOcclusionGridSideVoxels / 2;
    constexpr std::size_t kSize = kMaxLightOcclusionGridSideVoxels;
    const std::size_t x = static_cast<std::size_t>(wx - origin.x) + kHalf;
    const std::size_t y = static_cast<std::size_t>(wy - origin.y) + kHalf;
    const std::size_t z = static_cast<std::size_t>(wz - origin.z) + kHalf;
    return (z * kSize + y) * kSize + x;
}

inline void
gridSetBit(std::vector<std::uint32_t> &bitfield, int wx, int wy, int wz, const ivec3 &origin) {
    if (!gridInBounds(wx, wy, wz, origin))
        return;
    const std::size_t flat = gridFlatIndex(wx, wy, wz, origin);
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
    const IRMath::SDF::ShapeType shapeType = static_cast<IRMath::SDF::ShapeType>(shape.shapeType_);
    const vec4 effectiveParams = IRMath::SDF::effectiveParams(shapeType, shape.params_);
    const vec3 boundingHalf = IRMath::SDF::boundingHalf(shapeType, effectiveParams);

    constexpr int kHalf = kMaxLightOcclusionGridSideVoxels / 2;
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
    const int xMin = IRMath::max(
        static_cast<int>(IRMath::floor(shapeWorldPos.x - boundingHalf.x)) - 1,
        gridXMin
    );
    const int xMax =
        IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.x + boundingHalf.x)) + 1, gridXMax);
    const int yMin = IRMath::max(
        static_cast<int>(IRMath::floor(shapeWorldPos.y - boundingHalf.y)) - 1,
        gridYMin
    );
    const int yMax =
        IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.y + boundingHalf.y)) + 1, gridYMax);
    const int zMin = IRMath::max(
        static_cast<int>(IRMath::floor(shapeWorldPos.z - boundingHalf.z)) - 1,
        gridZMin
    );
    const int zMax =
        IRMath::min(static_cast<int>(IRMath::ceil(shapeWorldPos.z + boundingHalf.z)) + 1, gridZMax);

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
                    gridSetBit(bitfield, wx, wy, wz, origin);
                }
            }
        }
    }
}

/// Iterate every `C_ShapeDescriptor + C_LightBlocker + C_WorldTransform`
/// entity and rasterize the opt-in (`blocksLOS_=true`) blockers into the
/// supplied bitfield. Uses the batched-archetype-query pattern (no
/// per-entity getComponent in the tick). Returns the number of shapes
/// rasterized so the caller can skip the subData upload when no
/// blockers exist (the common case for demos that opt out at the
/// `C_LightBlocker{false, ...}` flag level).
inline std::size_t rasterizeAllBlockers(std::vector<std::uint32_t> &bitfield, const ivec3 &origin) {
    const auto include =
        IREntity::getArchetype<C_ShapeDescriptor, C_LightBlocker, C_WorldTransform>();
    const auto nodes = IREntity::queryArchetypeNodesSimple(include);
    std::size_t rasterized = 0;
    for (auto *node : nodes) {
        auto &shapes = IREntity::getComponentData<C_ShapeDescriptor>(node);
        auto &blockers = IREntity::getComponentData<C_LightBlocker>(node);
        auto &transforms = IREntity::getComponentData<C_WorldTransform>(node);
        for (int i = 0; i < node->length_; ++i) {
            if (!blockers[i].blocksLOS_)
                continue;
            rasterizeShapeBlocker(shapes[i], transforms[i].translation_, bitfield, origin);
            ++rasterized;
        }
    }
    return rasterized;
}

} // namespace detail

template <> struct System<BUILD_LIGHT_OCCLUSION_GRID> {
    Buffer *ssbo_ = nullptr;
    LightOcclusionGridHeader header_{};
    /// CPU mirror of the voxel-existence bitfield, allocated once
    /// at `create()` and reused every frame.
    std::vector<std::uint32_t> voxelBitfield_{};
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
    /// Camera-anchored origin of the current frame's grid. Set by
    /// the per-entity tick, consumed by the end-tick upload.
    ivec3 origin_{};
    /// Set true by the per-entity tick, false at the end of
    /// end-tick. Guards the SSBO upload when no canvas matched the
    /// archetype this frame (defensive — the system normally runs
    /// once per frame on the main canvas).
    bool ranThisFrame_ = false;

    void tick(IREntity::EntityId, C_VoxelPool &pool, const C_TrixelCanvasRenderBehavior &behavior) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        if (!behavior.useCameraPositionIso_)
            return;

        // Phase 1c (#360): re-center on the iso camera each frame. The CPU
        // bitfield producer and the GPU consumer both translate world→local
        // against `origin_` / `worldOriginVoxel` so the populate path stays
        // origin-agnostic. #2315 V1: freeze-aware — pins with the light
        // volume's anchor while frozen (engine/render/CLAUDE.md invariant
        // 1: this system reads the pinned ANCHOR only, never the cull
        // viewport).
        origin_ = IRRender::detail::frozenAwareCameraAnchorVoxel();

        {
            IR_PROFILE_BLOCK("BuildLightOcclusionGrid::Clear", IR_PROFILER_COLOR_RENDER);
            std::fill(voxelBitfield_.begin(), voxelBitfield_.end(), 0u);
        }

        const auto &globals = pool.getPositionGlobals();
        const auto &colors = pool.getColors();
        const int liveCount = pool.getLiveVoxelCount();

        {
            IR_PROFILE_BLOCK("BuildLightOcclusionGrid::Populate", IR_PROFILER_COLOR_RENDER);
            for (int i = 0; i < liveCount; ++i) {
                if (colors[i].color_.alpha_ == 0)
                    continue;
                const vec3 &wp = globals[i].pos_;
                // Round-half-up — must match the `roundHalfUp(...)`
                // helper in shaders/ir_iso_common.glsl + .metal. See
                // IRMath::roundVec3HalfUp doc-comment for why this
                // rule (vs std::lround / glm::round) is required for
                // the CPU↔GPU light-occlusion-grid handshake.
                const ivec3 cell = IRMath::roundVec3HalfUp(wp);
                detail::gridSetBit(voxelBitfield_, cell.x, cell.y, cell.z, origin_);
            }
        }

        ranThisFrame_ = true;
    }

    void beginTick() {
        ssbo_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_LightOcclusionGrid);
    }

    void endTick() {
        if (!ranThisFrame_)
            return;
        std::size_t blockerCount = 0;
        {
            IR_PROFILE_BLOCK("BuildLightOcclusionGrid::PopulateBlockers", IR_PROFILER_COLOR_RENDER);
            // Skip the 2 MB memset + rasterize when we know the
            // bitfield is already all-zero. After the last
            // blocker disappears we still need one trailing
            // clear+upload to drain stale bits from the GPU
            // bitfield, but every subsequent frame can skip
            // both the CPU fill and the GPU subData.
            if (prevFrameHadBlockers_) {
                std::fill(blockerBitfield_.begin(), blockerBitfield_.end(), 0u);
            }
            blockerCount = detail::rasterizeAllBlockers(blockerBitfield_, origin_);
        }

        {
            IR_PROFILE_BLOCK("BuildLightOcclusionGrid::Upload", IR_PROFILER_COLOR_RENDER);
            header_.worldOriginVoxel_ = ivec4(origin_.x, origin_.y, origin_.z, 0);
            ssbo_->subData(0, kLightOcclusionHeaderByteSize, &header_);
            ssbo_->subData(
                static_cast<std::ptrdiff_t>(kLightOcclusionHeaderByteSize),
                voxelBitfield_.size() * sizeof(std::uint32_t),
                voxelBitfield_.data()
            );
            // Upload the blocker region only when there are
            // blockers this frame, OR when there were blockers
            // last frame (so the GPU's stale bits are zeroed
            // exactly once on the transition to empty).
            if (blockerCount > 0 || prevFrameHadBlockers_) {
                ssbo_->subData(
                    static_cast<std::ptrdiff_t>(kLightBlockerBitfieldByteOffset),
                    blockerBitfield_.size() * sizeof(std::uint32_t),
                    blockerBitfield_.data()
                );
            }
        }
        prevFrameHadBlockers_ = (blockerCount > 0);
        ranThisFrame_ = false;
    }

    static SystemId create() {
        IRRender::createNamedResource<Buffer>(
            "LightOcclusionGridBuffer",
            nullptr,
            kLightOcclusionSSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_LightOcclusionGrid
        );

        SystemId systemId =
            registerSystem<BUILD_LIGHT_OCCLUSION_GRID, C_VoxelPool, C_TrixelCanvasRenderBehavior>(
                "BuildLightOcclusionGrid"
            );
        auto *p = getSystemParams<System<BUILD_LIGHT_OCCLUSION_GRID>>(systemId);
        p->ssbo_ = IRRender::getNamedResource<Buffer>("LightOcclusionGridBuffer");
        p->voxelBitfield_.assign(kLightOcclusionBitfieldUintCount, 0u);
        p->blockerBitfield_.assign(kLightOcclusionBitfieldUintCount, 0u);
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
        IRRender::tagGpuStage(systemId, "buildLightOcclusionGrid");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_LIGHT_OCCLUSION_GRID_H */
