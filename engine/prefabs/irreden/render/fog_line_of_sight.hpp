#ifndef IR_PREFAB_FOG_LINE_OF_SIGHT_H
#define IR_PREFAB_FOG_LINE_OF_SIGHT_H

// The fog line-of-sight model's CPU half: column rasterisation, the horizon
// trace, and the per-source horizon build. The model itself (occluder set,
// eye, horizon rule, gate, per-source tile anchoring) is stated once, in
// `component_canvas_fog_of_war.hpp`. `FOG_LOS_BUILD`, the reveal oracle and
// `IRPrefab::Fog::lineOfSight` all reach the rule through `traceLosHorizon`,
// so the point query and the built field agree exactly on one column set.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_job.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace IRPrefab::Fog {

/// Cells past `radius + edge` whose horizons are still built. A hard disc
/// (`edge` 0) lifts unexplored matter up to `kFogLosRimFadeCells` past its
/// radius (ir_fog_common's kFogRimFadeCells); an occluded source must
/// suppress that lift too, or the fade halo reappears behind the shadow at the
/// build's edge. The margin covers the antialiasing floor and a sample rounding
/// into a cell centre up to ~0.71 units farther out.
constexpr float kFogLosRimFadeCells = 8.0f;
constexpr float kFogLosDiscMargin = 2.0f;

/// One source's column-top view: the tile of `kFogLosColumnCount` tops at
/// `origin_`, in `FogLineOfSightField::columnIndex` order.
struct LosColumnView {
    int source_ = 0;
    IRMath::ivec2 origin_{0};
    std::span<std::int32_t> tops_;
};

using LosColumnViews = std::array<LosColumnView, IRComponents::kMaxFogVisionCircles>;

/// The distance from a disc's centre past which `buildLosHorizons` leaves the
/// field clear.
inline float losBuildReach(IRMath::vec4 circle) {
    const float edge = IRMath::max(circle.w, 0.0f);
    return circle.z + edge + (edge == 0.0f ? kFogLosRimFadeCells : 0.0f) + kFogLosDiscMargin;
}

/// Lower column @p cell's top to @p cell.z (the smallest Z is the highest
/// voxel) in the view anchored at @p tileOrigin. Cells outside the tile are
/// dropped.
inline void
stampLosColumn(std::span<std::int32_t> columnTops, IRMath::ivec2 tileOrigin, IRMath::ivec3 cell) {
    const IRMath::ivec2 column(cell);
    if (!IRComponents::FogLineOfSightField::cellInTile(column, tileOrigin))
        return;
    std::int32_t &top =
        columnTops[IRComponents::FogLineOfSightField::columnIndex(column, tileOrigin)];
    top = IRMath::min(top, static_cast<std::int32_t>(cell.z));
}

/// The views the gated sources of @p observers need, one per gated source,
/// over @p columnTops (`kFogLosColumnViewCount` tops: source `i`'s tile is the
/// `i`-th `kFogLosColumnCount` run). Returns the number of views filled.
inline int losSourceViews(
    const IRComponents::FrameDataFogObservers &observers,
    std::span<std::int32_t> columnTops,
    LosColumnViews &views
) {
    int count = 0;
    for (int source = 0; source < observers.visionCircleCount_; ++source) {
        if (((observers.losSourceMask_ >> source) & 1) == 0)
            continue;
        views[static_cast<std::size_t>(count++)] = LosColumnView{
            source,
            IRComponents::FogLineOfSightField::tileOrigin(observers.visionCircles_[source]),
            columnTops.subspan(
                static_cast<std::size_t>(source) * IRComponents::kFogLosColumnCount,
                IRComponents::kFogLosColumnCount
            )
        };
    }
    return count;
}

/// Rebuild every view in @p views from @p pool's occluding voxels and every
/// `blocksLOS_` shape on @p canvas (a shape whose `canvasEntity_` is unset
/// belongs to the active canvas, which the caller passes). Cost: one pass
/// over the live voxels testing each against every view's tile, plus about
/// one SDF evaluation per column of each flagged shape's footprint inside
/// each tile.
inline void rasterizeLosColumns(
    const IRComponents::C_VoxelPool &pool,
    IREntity::EntityId canvas,
    std::span<const LosColumnView> views
) {
    for (const LosColumnView &view : views) {
        std::fill(view.tops_.begin(), view.tops_.end(), IRComponents::kFogLosColumnEmpty);
    }
    if (views.empty())
        return;

    const IRRender::VoxelGpuPosition *positions = pool.getPositionGlobals().data();
    const IRComponents::C_Voxel *voxels = pool.getColors().data();
    const int liveCount = pool.getLiveVoxelCount();
    for (int i = 0; i < liveCount; ++i) {
        const IRComponents::C_Voxel &voxel = voxels[i];
        if (voxel.color_.alpha_ == 0 ||
            (voxel.reserved_ & IRComponents::VoxelReserved::kFogWholeBodyExempt) != 0u) {
            continue;
        }
        const IRMath::ivec3 cell = IRMath::roundVec3HalfUp(positions[i].pos_);
        for (const LosColumnView &view : views) {
            stampLosColumn(view.tops_, view.origin_, cell);
        }
    }

    constexpr int kUnclippedZ = std::numeric_limits<int>::max() / 2;
    const auto nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<
            IRComponents::C_ShapeDescriptor,
            IRComponents::C_LightBlocker,
            IRComponents::C_WorldTransform>()
    );
    for (auto *node : nodes) {
        const auto &shapes = IREntity::getComponentData<IRComponents::C_ShapeDescriptor>(node);
        const auto &blockers = IREntity::getComponentData<IRComponents::C_LightBlocker>(node);
        const auto &transforms = IREntity::getComponentData<IRComponents::C_WorldTransform>(node);
        for (int i = 0; i < node->length_; ++i) {
            const IRComponents::C_ShapeDescriptor &shape = shapes[i];
            if (!blockers[i].blocksLOS_ ||
                (shape.canvasEntity_ != IREntity::kNullEntity && shape.canvasEntity_ != canvas)) {
                continue;
            }
            for (const LosColumnView &view : views) {
                const IRMath::ivec3 clipMin(view.origin_.x, view.origin_.y, -kUnclippedZ);
                const IRMath::ivec3 clipMax(
                    view.origin_.x + IRComponents::kFogLosTileEdge - 1,
                    view.origin_.y + IRComponents::kFogLosTileEdge - 1,
                    kUnclippedZ
                );
                IRMath::SDF::forEachInteriorColumnTop(
                    static_cast<IRMath::SDF::ShapeType>(shape.shapeType_),
                    shape.params_,
                    transforms[i].translation_,
                    clipMin,
                    clipMax,
                    [&](IRMath::ivec3 cell) { stampLosColumn(view.tops_, view.origin_, cell); }
                );
            }
        }
    }
}

/// Horizon `H` of target cell @p target seen from @p eye over the view
/// @p columnTops anchored at @p tileOrigin (the header comment of
/// `component_canvas_fog_of_war.hpp` has the rule). `kFogLosHorizonClear`
/// when no occupied column lies strictly between the eye's cell and the
/// target, including when they are the same cell. Columns outside the tile
/// are empty.
inline float traceLosHorizon(
    std::span<const std::int32_t> columnTops,
    IRMath::ivec2 tileOrigin,
    IRMath::vec3 eye,
    IRMath::ivec2 target
) {
    using IRComponents::FogLineOfSightField;
    const int sourceX = IRMath::roundHalfUp(eye.x);
    const int sourceY = IRMath::roundHalfUp(eye.y);
    if (sourceX == target.x && sourceY == target.y)
        return IRComponents::kFogLosHorizonClear;

    const float deltaX = static_cast<float>(target.x) - eye.x;
    const float deltaY = static_cast<float>(target.y) - eye.y;
    const int stepX = deltaX > 0.0f ? 1 : (deltaX < 0.0f ? -1 : 0);
    const int stepY = deltaY > 0.0f ? 1 : (deltaY < 0.0f ? -1 : 0);
    constexpr float kNever = std::numeric_limits<float>::infinity();
    // Ray parameter (0 at the eye, 1 at the target centre) of the next cell
    // boundary on each axis; cells span [c - 0.5, c + 0.5).
    float tMaxX =
        stepX == 0
            ? kNever
            : (static_cast<float>(sourceX) + 0.5f * static_cast<float>(stepX) - eye.x) / deltaX;
    float tMaxY =
        stepY == 0
            ? kNever
            : (static_cast<float>(sourceY) + 0.5f * static_cast<float>(stepY) - eye.y) / deltaY;
    const float tDeltaX = stepX == 0 ? kNever : 1.0f / IRMath::abs(deltaX);
    const float tDeltaY = stepY == 0 ? kNever : 1.0f / IRMath::abs(deltaY);

    // Each column's horizon is `E.z + (T - E.z) * (d(t) / d(c))`, in double:
    // every term is monotone in its inputs, so a column no farther from the eye
    // than the target (`d(c) <= d(t)`, always true on the walk) yields exactly
    // `H >= T` — flat ground can never hide itself by rounding, even when the
    // eye sits on a cell corner and a side cell is as far as the target. The
    // float the texture stores rounds that bound monotonically.
    const double originX = static_cast<double>(eye.x);
    const double originY = static_cast<double>(eye.y);
    const double eyeZ = static_cast<double>(eye.z);
    const double targetDistance = IRMath::planarLength(
        static_cast<double>(target.x) - originX,
        static_cast<double>(target.y) - originY
    );
    double horizon = std::numeric_limits<double>::infinity();
    // Every visited cell lies in the box spanned by the eye's cell and the
    // target, so the per-cell tile test is needed only when that box leaves
    // the tile.
    const bool boxInTile =
        FogLineOfSightField::cellInTile(IRMath::ivec2(sourceX, sourceY), tileOrigin) &&
        FogLineOfSightField::cellInTile(target, tileOrigin);
    const std::int32_t *tops = columnTops.data();
    const auto visit = [&](int x, int y) {
        const IRMath::ivec2 cell(x, y);
        if (!boxInTile && !FogLineOfSightField::cellInTile(cell, tileOrigin))
            return;
        const std::int32_t top = tops[FogLineOfSightField::columnIndex(cell, tileOrigin)];
        if (top == IRComponents::kFogLosColumnEmpty)
            return;
        const double distance = IRMath::planarLength(
            static_cast<double>(x) - originX,
            static_cast<double>(y) - originY
        );
        if (distance <= 0.0)
            return;
        const double columnHorizon =
            eyeZ + (static_cast<double>(top) - eyeZ) * (targetDistance / distance);
        if (columnHorizon < horizon)
            horizon = columnHorizon;
    };

    // Each axis stops once it reaches the target's row/column, so the walk
    // always lands on the target in at most |dx| + |dy| steps.
    int cellX = sourceX;
    int cellY = sourceY;
    if (cellX == target.x)
        tMaxX = kNever;
    if (cellY == target.y)
        tMaxY = kNever;
    while (true) {
        if (tMaxX < tMaxY) {
            cellX += stepX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxX) {
            cellY += stepY;
            tMaxY += tDeltaY;
        } else {
            visit(cellX + stepX, cellY);
            visit(cellX, cellY + stepY);
            cellX += stepX;
            cellY += stepY;
            tMaxX += tDeltaX;
            tMaxY += tDeltaY;
        }
        if (cellX == target.x)
            tMaxX = kNever;
        if (cellY == target.y)
            tMaxY = kNever;
        if (cellX == target.x && cellY == target.y)
            break;
        visit(cellX, cellY);
    }

    if (horizon >= static_cast<double>(IRComponents::kFogLosHorizonClear))
        return IRComponents::kFogLosHorizonClear;
    return static_cast<float>(horizon);
}

/// The eye of vision circle @p source.
inline IRMath::vec3 losEye(
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLosEyeHeights &eyeHeights,
    int source
) {
    const IRMath::vec4 circle = observers.visionCircles_[source];
    return IRMath::vec3(
        circle.x,
        circle.y,
        observers.visionCircleHeights_[source].x - eyeHeights[static_cast<std::size_t>(source)]
    );
}

/// Fill @p horizons (the `losTexture_` texel image) for @p observers: clear
/// everywhere, then each view's source's horizon at every cell of its tile
/// within `losBuildReach` of its centre, traced over that view (`views` as
/// `losSourceViews` fills them, already rasterised). (view, row) pairs fan out
/// over the job pool (serial with none); every cell and source owns its own
/// float.
inline void buildLosHorizons(
    const IRComponents::FrameDataFogObservers &observers,
    const IRComponents::FogLosEyeHeights &eyeHeights,
    std::span<const LosColumnView> views,
    std::span<float> horizons
) {
    using IRComponents::FogLineOfSightField;
    std::fill(horizons.begin(), horizons.end(), IRComponents::kFogLosHorizonClear);

    struct SourceBuild {
        const LosColumnView *view_ = nullptr;
        IRMath::vec2 centre_;
        IRMath::vec3 eye_;
        float reachSq_;
        int xMin_, xMax_, yMin_, yMax_;
        int rowOffset_;
    };
    std::array<SourceBuild, IRComponents::kMaxFogVisionCircles> builds{};
    int buildCount = 0;
    // Work items are (view, row) pairs: `rowOffset_` is where a view's rows
    // start in the flat item range.
    int itemCount = 0;
    for (const LosColumnView &view : views) {
        const IRMath::vec4 circle = observers.visionCircles_[view.source_];
        const float reach = losBuildReach(circle);
        SourceBuild &build = builds[static_cast<std::size_t>(buildCount++)];
        build.view_ = &view;
        build.centre_ = IRMath::vec2(circle);
        build.eye_ = losEye(observers, eyeHeights, view.source_);
        build.reachSq_ = reach * reach;
        const int tileLast = IRComponents::kFogLosTileEdge - 1;
        build.xMin_ =
            IRMath::max(static_cast<int>(IRMath::floor(circle.x - reach)), view.origin_.x);
        build.xMax_ = IRMath::min(
            static_cast<int>(IRMath::ceil(circle.x + reach)),
            view.origin_.x + tileLast
        );
        build.yMin_ =
            IRMath::max(static_cast<int>(IRMath::floor(circle.y - reach)), view.origin_.y);
        build.yMax_ = IRMath::min(
            static_cast<int>(IRMath::ceil(circle.y + reach)),
            view.origin_.y + tileLast
        );
        build.rowOffset_ = itemCount;
        itemCount += IRMath::max(build.yMax_ - build.yMin_ + 1, 0);
    }
    if (itemCount == 0)
        return;

    IRJob::ParallelTuning tuning{};
    tuning.minItemsToParallelize_ = 8;
    tuning.minChunk_ = 4;
    IRJob::parallelForAutoGrain(
        itemCount,
        [&](int itemBegin, int itemEnd) {
            int b = 0;
            for (int item = itemBegin; item < itemEnd; ++item) {
                while (b + 1 < buildCount &&
                       item >= builds[static_cast<std::size_t>(b + 1)].rowOffset_)
                    ++b;
                const SourceBuild &build = builds[static_cast<std::size_t>(b)];
                const LosColumnView &view = *build.view_;
                const int y = build.yMin_ + (item - build.rowOffset_);
                const float dy = static_cast<float>(y) - build.centre_.y;
                for (int x = build.xMin_; x <= build.xMax_; ++x) {
                    const float dx = static_cast<float>(x) - build.centre_.x;
                    if (dx * dx + dy * dy > build.reachSq_)
                        continue;
                    const IRMath::ivec2 cell(x, y);
                    horizons[FogLineOfSightField::horizonIndex(view.source_, cell, view.origin_)] =
                        traceLosHorizon(view.tops_, view.origin_, build.eye_, cell);
                }
            }
        },
        tuning
    );
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_LINE_OF_SIGHT_H */
