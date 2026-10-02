#ifndef IR_PREFAB_FOG_LINE_OF_SIGHT_H
#define IR_PREFAB_FOG_LINE_OF_SIGHT_H

// The fog line-of-sight model's CPU half: the column-field raster, its
// pyramid and the exact segment march every consumer shares. The model itself (occluder set,
// eye, gate, sample mapping) is stated once, in
// `component_canvas_fog_of_war.hpp`. `FOG_LOS_BUILD`, the reveal oracle and
// the point queries (`IRPrefab::Fog::lineOfSight`, `LineOfSightView`) all
// reach the rule through `traceLosClearance`,
// and the shader twins (`ir_fog_los.{glsl,metal}`) walk the same lattice with
// the same arithmetic, so a pixel and an entity anchor agree on one column
// set.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace IRPrefab::Fog {

/// Cells past `radius + edge` a source can still affect. A hard disc
/// (`edge` 0) lifts unexplored matter up to `kFogLosRimFadeCells` past its
/// radius (ir_fog_common's kFogRimFadeCells); an occluded source must
/// suppress that lift too, or the fade halo reappears behind the shadow. The
/// margin covers the antialiasing floor.
constexpr float kFogLosRimFadeCells = 8.0f;
constexpr float kFogLosDiscMargin = 2.0f;

/// The distance from a disc's centre past which a sample is not gated.
/// Mirrors `fogLosReach` in the shader twins.
inline float losReach(IRMath::vec4 circle) {
    const float edge = IRMath::max(circle.w, 0.0f);
    return circle.z + edge + (edge == 0.0f ? kFogLosRimFadeCells : 0.0f) + kFogLosDiscMargin;
}

/// How the active canvas rasterizes this frame — what the column field must
/// reproduce so a column box lies where its voxel or shape is drawn. At a
/// cardinal pose a voxel snaps to the `1 / subdivisions_` lattice and a shape
/// origin to the integer view lattice; while the camera turns (per-axis
/// voxels, smooth-yaw shapes) both keep their continuous positions.
struct LosRasterFrame {
    IRMath::CardinalIndex cardinal_ = IRMath::CardinalIndex::k0;
    int subdivisions_ = 1;
    bool rotating_ = false;
};

namespace detail {

/// The fog window origin for a canvas of @p canvasSize this frame: the window
/// is centred on the world point at z = 0 under the canvas's viewport centre
/// for the live effective camera and yaw, snapped to field chunks. The gather
/// and `FOG_LOS_BUILD` both call it after the camera systems, so they agree
/// on the origin within a frame.
inline IRMath::ivec2 cameraWindowOrigin(int edge, IRMath::ivec2 canvasSize) {
    const IRMath::IsoBounds2D viewport = IRMath::visibleIsoViewport(
        IRRender::getEffectiveCameraIso(),
        IRMath::trixelOriginOffsetZ1(canvasSize),
        canvasSize
    );
    const IRMath::vec2 centreIso = (viewport.min_ + viewport.max_) * 0.5f;
    const IRMath::vec3 centre =
        IRMath::pos2DIsoToPos3DAtZLevelYawed(centreIso, 0.0f, IRPrefab::Camera::getYaw());
    return windowOriginForCentre(IRMath::vec2(centre), edge);
}

} // namespace detail

/// The frame the active canvas renders with right now.
inline LosRasterFrame activeLosRasterFrame() {
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::getYawSplit();
    LosRasterFrame frame;
    frame.cardinal_ = IRMath::rasterYawCardinalIndex(rasterYaw);
    frame.rotating_ = residualYaw != 0.0f;
    frame.subdivisions_ = IRRender::getSubdivisionMode() == IRRender::SubdivisionMode::NONE
                              ? 1
                              : IRMath::max(IRRender::getVoxelRenderEffectiveSubdivisions(), 1);
    return frame;
}

/// The box the raster draws the voxel at pool position @p position as, in
/// the world coordinates every pixel recovers: the unit cube on the voxel's
/// lower-corner lattice. At a cardinal pose the position first snaps to the
/// `1 / subdivisions_` lattice and the cube is the cardinal-rotated view
/// cell, so at a quarter turn it extends toward -X instead of +X; while the
/// camera turns the per-axis store keeps the world axes and the continuous
/// position. The top plane is `boxMin.z`.
inline void losVoxelBox(
    IRMath::vec3 position, const LosRasterFrame &frame, IRMath::vec3 &boxMin, IRMath::vec3 &boxMax
) {
    if (frame.rotating_) {
        boxMin = position;
        boxMax = position + IRMath::vec3(1.0f);
        return;
    }
    const float scale = static_cast<float>(frame.subdivisions_);
    const IRMath::vec3 snapped =
        IRMath::vec3(
            IRMath::roundVec3HalfUp(IRMath::snapNearIntegerVoxelPosition(position) * scale)
        ) /
        scale;
    const IRMath::vec3 farCorner =
        IRMath::rotateCardinalZInv(IRMath::vec3(1.0f, 1.0f, 0.0f), frame.cardinal_);
    boxMin = snapped + IRMath::min(farCorner, IRMath::vec3(0.0f));
    boxMax = snapped + IRMath::max(farCorner, IRMath::vec3(0.0f)) + IRMath::vec3(0.0f, 0.0f, 1.0f);
}

/// The half-cell whose lower edge is nearest world coordinate @p world, the
/// lattice line a box edge snaps onto.
inline int losHalfCellEdge(float world) {
    return IRMath::roundHalfUp(world * static_cast<float>(IRComponents::kFogLosCellsPerUnit));
}

/// Lower the top planes of every half-cell in `[minXY, maxXY)` of the field
/// whose lower corner is @p fieldMin to @p topPlane (the smallest Z is the
/// highest). Out-of-field cells are dropped.
inline void stampLosBox(
    std::span<float> columnTops,
    IRMath::ivec2 fieldMin,
    IRMath::vec2 minXY,
    IRMath::vec2 maxXY,
    float topPlane
) {
    using IRComponents::FogLosColumnField;
    using IRComponents::kFogLosFieldSize;
    const int xBegin = IRMath::max(losHalfCellEdge(minXY.x), fieldMin.x);
    const int xEnd = IRMath::min(losHalfCellEdge(maxXY.x), fieldMin.x + kFogLosFieldSize);
    const int yBegin = IRMath::max(losHalfCellEdge(minXY.y), fieldMin.y);
    const int yEnd = IRMath::min(losHalfCellEdge(maxXY.y), fieldMin.y + kFogLosFieldSize);
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            float &top = columnTops[FogLosColumnField::columnIndex(IRMath::ivec2(x, y), fieldMin)];
            top = IRMath::min(top, topPlane);
        }
    }
}

/// Stamp one flagged shape: an unrotated box is stamped exactly at the
/// surface the shape raster draws (`bounding half + half a raster cell`);
/// any other shape samples its SDF at half-cell centres, so its footprint
/// and top plane are within a quarter unit of the drawn surface.
inline void stampLosShape(
    std::span<float> columnTops,
    IRMath::ivec2 fieldMin,
    const IRComponents::C_ShapeDescriptor &shape,
    IRMath::vec3 translation,
    IRMath::vec4 rotation,
    const LosRasterFrame &frame
) {
    using IRMath::SDF::ShapeType;
    const ShapeType type = static_cast<ShapeType>(shape.shapeType_);
    const IRMath::vec4 params = IRMath::SDF::effectiveParams(type, shape.params_);
    const float dilation = 0.5f / static_cast<float>(frame.subdivisions_);
    IRMath::vec3 centre = translation;
    if (!frame.rotating_) {
        centre = IRMath::rotateCardinalZInv(
            IRMath::vec3(
                IRMath::roundVec3HalfUp(IRMath::rotateCardinalZ(translation, frame.cardinal_))
            ),
            frame.cardinal_
        );
    }
    const bool rotated = IRMath::abs(rotation.w) < 0.9999f;
    IRMath::vec3 half = IRMath::SDF::boundingHalf(type, params) + IRMath::vec3(dilation);
    if (type == ShapeType::BOX && !rotated) {
        stampLosBox(
            columnTops,
            fieldMin,
            IRMath::vec2(centre) - IRMath::vec2(half),
            IRMath::vec2(centre) + IRMath::vec2(half),
            centre.z - half.z
        );
        return;
    }
    if (rotated) {
        const IRMath::vec3 ax =
            IRMath::abs(IRMath::rotateVectorByQuat(IRMath::vec3(half.x, 0.0f, 0.0f), rotation));
        const IRMath::vec3 ay =
            IRMath::abs(IRMath::rotateVectorByQuat(IRMath::vec3(0.0f, half.y, 0.0f), rotation));
        const IRMath::vec3 az =
            IRMath::abs(IRMath::rotateVectorByQuat(IRMath::vec3(0.0f, 0.0f, half.z), rotation));
        half = ax + ay + az;
    }
    const IRMath::vec4 inverseRotation(-rotation.x, -rotation.y, -rotation.z, rotation.w);
    constexpr float kHalfCell = 1.0f / static_cast<float>(IRComponents::kFogLosCellsPerUnit);
    const int xBegin = losHalfCellEdge(centre.x - half.x);
    const int xEnd = losHalfCellEdge(centre.x + half.x);
    const int yBegin = losHalfCellEdge(centre.y - half.y);
    const int yEnd = losHalfCellEdge(centre.y + half.y);
    const int zBegin = losHalfCellEdge(centre.z - half.z);
    const int zEnd = losHalfCellEdge(centre.z + half.z);
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            const IRMath::vec2 cellCentre(
                (static_cast<float>(x) + 0.5f) * kHalfCell,
                (static_cast<float>(y) + 0.5f) * kHalfCell
            );
            for (int z = zBegin; z < zEnd; ++z) {
                IRMath::vec3 local =
                    IRMath::vec3(cellCentre, (static_cast<float>(z) + 0.5f) * kHalfCell) - centre;
                if (rotated) {
                    local = IRMath::rotateVectorByQuat(local, inverseRotation);
                }
                if (IRMath::SDF::evaluate(local, type, params) > dilation) {
                    continue;
                }
                stampLosBox(
                    columnTops,
                    fieldMin,
                    cellCentre - IRMath::vec2(0.5f * kHalfCell),
                    cellCentre + IRMath::vec2(0.5f * kHalfCell),
                    static_cast<float>(z) * kHalfCell
                );
                break;
            }
        }
    }
}

/// Fill the pyramid levels of @p columnTops from its level 0: every block of
/// level `k` takes the highest top plane (the smallest Z) of its four level
/// `k - 1` children. Every producer of a field runs this after its stamps,
/// so the march's block tests are sound.
inline void buildLosPyramid(std::span<float> columnTops) {
    using IRComponents::FogLosColumnField;
    for (int level = 1; level < IRComponents::kFogLosLevelCount; ++level) {
        const std::size_t blocks =
            static_cast<std::size_t>(IRComponents::kFogLosFieldSize >> level);
        for (std::size_t y = 0; y < blocks; ++y) {
            for (std::size_t x = 0; x < blocks; ++x) {
                const float top = IRMath::min(
                    IRMath::min(
                        columnTops[FogLosColumnField::blockIndex(level - 1, 2u * x, 2u * y)],
                        columnTops[FogLosColumnField::blockIndex(level - 1, 2u * x + 1u, 2u * y)]
                    ),
                    IRMath::min(
                        columnTops[FogLosColumnField::blockIndex(level - 1, 2u * x, 2u * y + 1u)],
                        columnTops
                            [FogLosColumnField::blockIndex(level - 1, 2u * x + 1u, 2u * y + 1u)]
                    )
                );
                columnTops[FogLosColumnField::blockIndex(level, x, y)] = top;
            }
        }
    }
}

/// Rebuild @p columnTops, the field whose lower corner is @p fieldMin, from
/// @p pool's occluding voxels and every `blocksLOS_` shape on @p canvas (a
/// shape whose `canvasEntity_` is unset belongs to the active canvas, which
/// the caller passes), each at the box the raster draws it as under @p frame,
/// then its pyramid. Cost: one pass over the live voxels (four half-cells
/// each) plus one SDF pass per flagged shape's footprint, plus a third of the
/// field again for the pyramid.
inline void rasterizeLosColumns(
    const IRComponents::C_VoxelPool &pool,
    IREntity::EntityId canvas,
    const LosRasterFrame &frame,
    IRMath::ivec2 fieldMin,
    std::span<float> columnTops
) {
    std::fill(columnTops.begin(), columnTops.end(), IRComponents::kFogLosColumnEmpty);

    const IRRender::VoxelGpuPosition *positions = pool.getPositionGlobals().data();
    const IRComponents::C_Voxel *voxels = pool.getColors().data();
    const int liveCount = pool.getLiveVoxelCount();
    for (int i = 0; i < liveCount; ++i) {
        const IRComponents::C_Voxel &voxel = voxels[i];
        if (voxel.color_.alpha_ == 0 ||
            (voxel.reserved_ & IRComponents::VoxelReserved::kFogWholeBodyExempt) != 0u) {
            continue;
        }
        IRMath::vec3 boxMin;
        IRMath::vec3 boxMax;
        losVoxelBox(positions[i].pos_, frame, boxMin, boxMax);
        stampLosBox(columnTops, fieldMin, IRMath::vec2(boxMin), IRMath::vec2(boxMax), boxMin.z);
    }

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
            stampLosShape(
                columnTops,
                fieldMin,
                shape,
                transforms[i].translation_,
                transforms[i].rotation_,
                frame
            );
        }
    }
    buildLosPyramid(columnTops);
}

/// Upper bound on the walk's steps for one segment inside the field: a
/// level-0 cell costs at most one climb and one descent around it.
constexpr int kFogLosMaxMarchSteps = 8 * IRComponents::kFogLosFieldSize;
/// How far past a lattice line the walk's position is taken, in half-cells:
/// a position on the line belongs to the cell ahead, beyond float rounding.
constexpr float kFogLosMarchNudge = 4.0e-3f;

/// The signed height by which the segment from an eye at height @p eyeZ,
/// rising @p rise to its target, passes over a column top @p top at segment
/// parameter @p tLow; negative when it passes below. Every verdict, marched
/// or summarized, evaluates this one expression, so they round (and
/// contract a multiply-add) alike.
inline float losSegmentClearance(float top, float eyeZ, float tLow, float rise) {
    return top - (eyeZ + tLow * rise);
}

/// Whether the column top @p top hides the segment at @p tLow: the hard
/// gate's test on `losSegmentClearance`. Monotone in @p rise for a fixed
/// non-negative @p tLow — once true, true at every larger rise.
inline bool losSegmentBlocked(float top, float eyeZ, float tLow, float rise) {
    return losSegmentClearance(top, eyeZ, tLow, rise) < -IRComponents::kFogLosClearanceTolerance;
}

/// Walks the half-cell route of the XY segment from @p eye to @p target over
/// @p field's pyramid. At level `L` the walk stands in the block of `2^L`
/// half-cells holding its position; `t` and `tExit` are the segment
/// parameters where it entered and leaves that block. A non-empty block
/// above level 0 is entered a level finer only when @p enter(top, t, tExit)
/// says it might matter; otherwise it is stepped over whole and the walk
/// climbs to the coarsest level whose block ahead is new (the crossing lies
/// on its boundary). Every non-empty level-0 cell but the eye's own goes to
/// @p visit(top, t, tExit), which returns true to stop. A level-0 cell's
/// `t` and `tExit` are the flat walk's whatever was stepped over before it,
/// so an @p enter that never declines a block holding a cell @p visit cares
/// about makes the walk exact. False when the step bound ran out first.
template <typename EnterFn, typename VisitFn>
inline bool walkLosRoute(
    const IRComponents::FogLosColumnField &field,
    IRMath::vec2 eye,
    IRMath::vec2 target,
    EnterFn &&enter,
    VisitFn &&visit
) {
    using IRComponents::FogLosColumnField;
    constexpr float kCells = static_cast<float>(IRComponents::kFogLosCellsPerUnit);
    constexpr float kNever = IRComponents::kFogLosColumnEmpty;
    constexpr int kMaxLevel = IRComponents::kFogLosLevelCount - 1;
    const IRMath::vec2 start = eye * kCells;
    const IRMath::vec2 delta = target * kCells - start;
    const IRMath::ivec2 step(
        delta.x > 0.0f ? 1 : (delta.x < 0.0f ? -1 : 0),
        delta.y > 0.0f ? 1 : (delta.y < 0.0f ? -1 : 0)
    );
    const IRMath::vec2 nudge =
        IRMath::vec2(static_cast<float>(step.x), static_cast<float>(step.y)) * kFogLosMarchNudge;
    // The eye's own half-cell is `halfCellOf` its position; the walk starts
    // in the cell ahead of it, which is the same cell off a lattice line.
    const IRMath::ivec2 eyeCell(
        static_cast<int>(IRMath::floor(start.x)),
        static_cast<int>(IRMath::floor(start.y))
    );
    float t = 0.0f;
    IRMath::ivec2 cell(
        static_cast<int>(IRMath::floor(start.x + nudge.x)),
        static_cast<int>(IRMath::floor(start.y + nudge.y))
    );
    int level = 0;
    for (int i = 0; i < kFogLosMaxMarchSteps; ++i) {
        const float size = static_cast<float>(1 << level);
        const IRMath::ivec2 blockMin(
            FogLosColumnField::blockMin(level, cell.x),
            FogLosColumnField::blockMin(level, cell.y)
        );
        const IRMath::vec2 exitEdge(
            static_cast<float>(blockMin.x) + (step.x > 0 ? size : 0.0f),
            static_cast<float>(blockMin.y) + (step.y > 0 ? size : 0.0f)
        );
        const IRMath::vec2 tEdge(
            step.x == 0 ? kNever : (exitEdge.x - start.x) / delta.x,
            step.y == 0 ? kNever : (exitEdge.y - start.y) / delta.y
        );
        const float tExit = IRMath::min(IRMath::min(tEdge.x, tEdge.y), 1.0f);
        const float top = (level == 0 && cell.x == eyeCell.x && cell.y == eyeCell.y)
                              ? kNever
                              : field.blockTop(level, blockMin.x, blockMin.y);
        if (top != kNever) {
            if (level == 0) {
                if (visit(top, t, tExit)) {
                    return true;
                }
            } else if (enter(top, t, tExit)) {
                --level;
                continue;
            }
        }
        if (tExit >= 1.0f) {
            return true;
        }
        t = tExit;
        const IRMath::vec2 next = start + delta * t + nudge;
        cell = IRMath::ivec2(
            static_cast<int>(IRMath::floor(next.x)),
            static_cast<int>(IRMath::floor(next.y))
        );
        // Climb while the crossing lies on a coarser block's boundary: only
        // then is the block ahead one the walk has not already found blocking.
        const bool crossedX = tEdge.x <= tEdge.y;
        const bool crossedY = tEdge.y <= tEdge.x;
        const int edgeX = static_cast<int>(exitEdge.x) + IRComponents::kFogLosLevelBias;
        const int edgeY = static_cast<int>(exitEdge.y) + IRComponents::kFogLosLevelBias;
        for (; level < kMaxLevel; ++level) {
            const int mask = (2 << level) - 1;
            if (!((crossedX && (edgeX & mask) == 0) || (crossedY && (edgeY & mask) == 0))) {
                break;
            }
        }
    }
    return false;
}

/// The smallest clearance the segment from @p eye to @p target keeps above
/// any column it crosses (the header comment of
/// `component_canvas_fog_of_war.hpp` has the rule); positive means the
/// segment passes above every column, `kFogLosColumnEmpty` when it crosses
/// none. The eye's own half-cell never counts. Only columns whose top plane
/// lies strictly above @p target's height enter @p bandClearance, the value
/// @p softness grades; @p target's own surface and the ground around it
/// therefore never soften a sample that rests on them.
///
/// The walk is `walkLosRoute`: a block's highest top against the segment's
/// lowest point over the block bounds every clearance inside it from below,
/// and a block whose bound cannot lower the verdict — it is at least
/// `-kFogLosClearanceTolerance`, and at least `min(bandClearance, softness)`
/// when the block holds band columns and the gate is soft — is stepped over.
/// A level-0 cell is evaluated exactly. So the result is exact whenever it is
/// below the tolerance, @p bandClearance whenever it is below @p softness,
/// and each is otherwise at least that threshold: the gate's factor is the
/// flat march's. Mirrors `fogLosTraceClearance` in the shader twins, step
/// for step.
inline float traceLosClearance(
    const IRComponents::FogLosColumnField &field,
    IRMath::vec3 eye,
    IRMath::vec3 target,
    float softness,
    float &bandClearance
) {
    using IRComponents::kFogLosClearanceTolerance;
    const float rise = target.z - eye.z;
    const float bandHorizon = target.z - kFogLosClearanceTolerance;
    float minClearance = IRComponents::kFogLosColumnEmpty;
    bandClearance = IRComponents::kFogLosColumnEmpty;
    walkLosRoute(
        field,
        IRMath::vec2(eye),
        IRMath::vec2(target),
        [&](float top, float t, float tExit) {
            const float clearance = losSegmentClearance(top, eye.z, rise > 0.0f ? tExit : t, rise);
            const float needed = (top < bandHorizon && softness > 0.0f)
                                     ? IRMath::min(bandClearance, softness)
                                     : -kFogLosClearanceTolerance;
            return clearance < needed;
        },
        [&](float top, float t, float tExit) {
            const float clearance = losSegmentClearance(top, eye.z, rise > 0.0f ? tExit : t, rise);
            minClearance = IRMath::min(minClearance, clearance);
            if (top < bandHorizon) {
                bandClearance = IRMath::min(bandClearance, clearance);
            }
            return clearance < -kFogLosClearanceTolerance;
        }
    );
    return minClearance;
}

/// The gate's factor for the clearances `traceLosClearance` returned: a step
/// at `kFogLosHardGate`, else `smoothstep(0, softness, bandClearance)` for a
/// segment nothing blocks. Mirrors `fogLosVisibilityFromClearance`.
inline float losVisibilityFromClearance(float minClearance, float bandClearance, float softness) {
    if (minClearance < -IRComponents::kFogLosClearanceTolerance) {
        return 0.0f;
    }
    if (softness <= IRComponents::kFogLosHardGate) {
        return 1.0f;
    }
    return IRMath::smoothstep(0.0f, softness, bandClearance);
}

/// The eye of vision circle @p source.
inline IRMath::vec3 losEye(const IRComponents::FrameDataFogObservers &observers, int source) {
    const IRMath::vec4 circle = observers.visionCircles_[source];
    return IRMath::vec3(
        circle.x,
        circle.y,
        observers.visionCircleHeights_[source].x - observers.losEyeHeight(source)
    );
}

/// Source @p source's line-of-sight factor at @p position over @p field:
/// the segment march with the source's softness, 1 past the source's reach.
/// A point at or below its own column's top plane is evaluated on that plane,
/// so an anchor resting on (or authored inside) the ground reads its surface.
/// 0 for an unpublished field.
inline float losVisibility(
    const IRComponents::FogLosColumnField &field,
    const IRComponents::FrameDataFogObservers &observers,
    int source,
    IRMath::vec3 position
) {
    if (!field.published()) {
        return 0.0f;
    }
    const IRMath::vec4 circle = observers.visionCircles_[source];
    if (IRMath::length(IRMath::vec2(position) - IRMath::vec2(circle)) > losReach(circle)) {
        return 1.0f;
    }
    const float ownTop = field.topPlane(
        IRComponents::FogLosColumnField::halfCellOf(position.x),
        IRComponents::FogLosColumnField::halfCellOf(position.y)
    );
    const IRMath::vec3 target(position.x, position.y, IRMath::min(position.z, ownTop));
    float bandClearance = 0.0f;
    const float minClearance = traceLosClearance(
        field,
        losEye(observers, source),
        target,
        observers.losSoftness(source),
        bandClearance
    );
    return losVisibilityFromClearance(minClearance, bandClearance, observers.losSoftness(source));
}

/// One slope regime of a `LosHardRoute`: a sample whose rise — its height
/// less the eye's, +Z down — lies below `clearBelow_` is visible, and one at
/// or above `blockedFrom_` is hidden. A rise between them is left to the
/// march.
struct LosRiseBand {
    float clearBelow_ = std::numeric_limits<float>::infinity();
    float blockedFrom_ = std::numeric_limits<float>::infinity();
};

/// A hard-gated source's verdict at every height over one exact target XY:
/// the target column's top plane and, for each regime of
/// `traceLosClearance`, the band a rise falls in. `belowEye_` covers a rise
/// above 0, where the march tests each column at the segment's exit;
/// `atOrAboveEye_` covers the rest, tested at each column's entry. The
/// default is the out-of-reach route, visible at every height.
struct LosHardRoute {
    float ownTop_ = IRComponents::kFogLosColumnEmpty;
    LosRiseBand belowEye_;
    LosRiseBand atOrAboveEye_;
};

namespace detail {

/// One regime's level-0 cells along a route, against the rises it can see,
/// `[low_, high_]`. Each cell's `losSegmentBlocked` is monotone over that
/// range: a cell clear at `high_` never blocks, one blocked at `low_` always
/// does, and any other turns blocked once, near its seed (the real-valued
/// root of the test). The earliest seed bounds the regime's transition.
struct LosRegimeScan {
    float low_ = 0.0f;
    float high_ = 0.0f;
    bool atExit_ = false;
    bool always_ = false;
    int transitions_ = 0;
    float seed_ = std::numeric_limits<float>::infinity();
    float seedTop_ = 0.0f;
    float seedTLow_ = 0.0f;

    bool mayBlock(float top, float eyeZ, float t, float tExit) const {
        return low_ <= high_ && losSegmentBlocked(top, eyeZ, atExit_ ? tExit : t, high_);
    }

    void visit(float top, float eyeZ, float t, float tExit) {
        if (!mayBlock(top, eyeZ, t, tExit)) {
            return;
        }
        const float tLow = atExit_ ? tExit : t;
        if (losSegmentBlocked(top, eyeZ, tLow, low_)) {
            always_ = true;
            return;
        }
        ++transitions_;
        const float seed = (top - eyeZ + IRComponents::kFogLosClearanceTolerance) / tLow;
        if (seed < seed_) {
            seed_ = seed;
            seedTop_ = top;
            seedTLow_ = tLow;
        }
    }

    /// The narrowest band this scan proves without another walk: `[low_,
    /// high_]` for a transition (clear at `low_`, the seed cell blocked at
    /// `high_`), else the regime's constant verdict.
    LosRiseBand provenBand() const {
        constexpr float kInf = std::numeric_limits<float>::infinity();
        if (always_) {
            return {-kInf, -kInf};
        }
        if (transitions_ == 0) {
            return {kInf, kInf};
        }
        return {low_, high_};
    }

    /// Half the width of a candidate band around the earliest seed: the
    /// rounding the seed and the test can each carry, generously, times
    /// @p widen.
    float candidateSpread(float eyeZ, float widen) const {
        constexpr float kRelative = 1.0f / static_cast<float>(1 << 19);
        const float scale =
            (IRMath::abs(seedTop_) + IRMath::abs(eyeZ) + IRComponents::kFogLosClearanceTolerance) /
                seedTLow_ +
            IRMath::abs(IRMath::clamp(seed_, low_, high_));
        return scale * kRelative * widen;
    }
};

} // namespace detail

/// Summarize source @p source's hard gate over the exact target XY @p target
/// (`LosHardRoute`), from the same publication `losVisibility` reads. The
/// route's cells, their `t` / `tExit` and the target column's top depend on
/// XY alone, so one walk serves every height: the pyramid steps over blocks
/// that cannot block at either regime's extreme rise, and the cells it enters
/// bound each regime's transition. A band edge is kept only where
/// `losSegmentBlocked` itself proves it — the earliest seed's cell blocked
/// at `blockedFrom_`, every cell clear at `clearBelow_` (a second walk when
/// more than one cell transitions) — so a verdict the summary gives is the
/// march's bit for bit, and a rise it cannot prove is left to the march.
/// Cost: one walk, plus one per band that needs widening.
inline LosHardRoute buildLosHardRoute(
    const IRComponents::FogLosColumnField &field,
    const IRComponents::FrameDataFogObservers &observers,
    int source,
    IRMath::vec2 target
) {
    constexpr float kInf = std::numeric_limits<float>::infinity();
    LosHardRoute route;
    if (!field.published()) {
        route.belowEye_ = {-kInf, -kInf};
        route.atOrAboveEye_ = {-kInf, -kInf};
        return route;
    }
    const IRMath::vec4 circle = observers.visionCircles_[source];
    if (IRMath::length(target - IRMath::vec2(circle)) > losReach(circle)) {
        return route;
    }
    route.ownTop_ = field.topPlane(
        IRComponents::FogLosColumnField::halfCellOf(target.x),
        IRComponents::FogLosColumnField::halfCellOf(target.y)
    );
    const IRMath::vec3 eye = losEye(observers, source);
    // A sample's rise never exceeds its column top less the eye height: the
    // march lifts it onto that top first.
    detail::LosRegimeScan regimes[2];
    regimes[0].low_ = std::numeric_limits<float>::denorm_min();
    regimes[0].high_ = route.ownTop_ - eye.z;
    regimes[0].atExit_ = true;
    regimes[1].low_ = -std::numeric_limits<float>::max();
    regimes[1].high_ = 0.0f;
    const auto enter = [&](float top, float t, float tExit) {
        return regimes[0].mayBlock(top, eye.z, t, tExit) ||
               regimes[1].mayBlock(top, eye.z, t, tExit);
    };
    const bool walked =
        walkLosRoute(field, IRMath::vec2(eye), target, enter, [&](float top, float t, float tExit) {
            regimes[0].visit(top, eye.z, t, tExit);
            regimes[1].visit(top, eye.z, t, tExit);
            return false;
        });
    if (!walked) {
        route.belowEye_ = {-kInf, kInf};
        route.atOrAboveEye_ = {-kInf, kInf};
        return route;
    }

    // Narrow each transitioning regime's proven `[low_, high_]` toward its
    // earliest seed. A candidate edge that fails its proof is retried wider;
    // one that reaches the regime's bound keeps the bound.
    LosRiseBand bands[2] = {regimes[0].provenBand(), regimes[1].provenBand()};
    bool highDone[2];
    bool lowDone[2];
    for (int r = 0; r < 2; ++r) {
        highDone[r] = lowDone[r] = regimes[r].always_ || regimes[r].transitions_ == 0;
    }
    constexpr float kWiden[] = {1.0f, 64.0f, 4096.0f};
    for (const float widen : kWiden) {
        float candidateLow[2] = {0.0f, 0.0f};
        bool verify[2] = {false, false};
        for (int r = 0; r < 2; ++r) {
            const detail::LosRegimeScan &scan = regimes[r];
            const float seed = IRMath::clamp(scan.seed_, scan.low_, scan.high_);
            const float spread =
                highDone[r] && lowDone[r] ? 0.0f : scan.candidateSpread(eye.z, widen);
            if (!highDone[r]) {
                const float high = seed + spread;
                if (high >= scan.high_) {
                    highDone[r] = true;
                } else if (losSegmentBlocked(scan.seedTop_, eye.z, scan.seedTLow_, high)) {
                    bands[r].blockedFrom_ = high;
                    highDone[r] = true;
                }
            }
            if (!lowDone[r]) {
                candidateLow[r] = seed - spread;
                if (candidateLow[r] <= scan.low_) {
                    lowDone[r] = true;
                } else if (scan.transitions_ > 1) {
                    verify[r] = true;
                } else if (!losSegmentBlocked(
                               scan.seedTop_,
                               eye.z,
                               scan.seedTLow_,
                               candidateLow[r]
                           )) {
                    bands[r].clearBelow_ = candidateLow[r];
                    lowDone[r] = true;
                }
            }
        }
        if (verify[0] || verify[1]) {
            walkLosRoute(
                field,
                IRMath::vec2(eye),
                target,
                enter,
                [&](float top, float t, float tExit) {
                    for (int r = 0; r < 2; ++r) {
                        const detail::LosRegimeScan &scan = regimes[r];
                        if (verify[r] && losSegmentBlocked(
                                             top,
                                             eye.z,
                                             scan.atExit_ ? tExit : t,
                                             candidateLow[r]
                                         )) {
                            verify[r] = false;
                        }
                    }
                    return !verify[0] && !verify[1];
                }
            );
            for (int r = 0; r < 2; ++r) {
                if (verify[r]) {
                    bands[r].clearBelow_ = candidateLow[r];
                    lowDone[r] = true;
                }
            }
        }
        if (highDone[0] && highDone[1] && lowDone[0] && lowDone[1]) {
            break;
        }
    }
    route.belowEye_ = bands[0];
    route.atOrAboveEye_ = bands[1];
    return route;
}

/// The hard gate's verdict over @p route for a sample at height
/// @p positionZ seen from an eye at height @p eyeZ (`losEye(...).z`): 1
/// visible, 0 hidden, -1 when only the march can decide. The rise is
/// computed as `losVisibility` computes it.
inline int losHardRouteVerdict(const LosHardRoute &route, float eyeZ, float positionZ) {
    const float rise = IRMath::min(positionZ, route.ownTop_) - eyeZ;
    const LosRiseBand &band = rise > 0.0f ? route.belowEye_ : route.atOrAboveEye_;
    if (rise < band.clearBelow_) {
        return 1;
    }
    if (rise >= band.blockedFrom_) {
        return 0;
    }
    return -1;
}

/// Whether @p to is visible from the eye @p from over the published @p field
/// at the hard gate, with @p to lifted onto its column's top plane when it
/// sits below it. True when @p to shares @p from's half-cell or lies outside
/// the field.
inline bool
losPointVisible(const IRComponents::FogLosColumnField &field, IRMath::vec3 from, IRMath::vec3 to) {
    using IRComponents::FogLosColumnField;
    const int toX = FogLosColumnField::halfCellOf(to.x);
    const int toY = FogLosColumnField::halfCellOf(to.y);
    if (!FogLosColumnField::cellInField(IRMath::ivec2(toX, toY), field.fieldMin_)) {
        return true;
    }
    float bandClearance = 0.0f;
    const float minClearance = traceLosClearance(
        field,
        from,
        IRMath::vec3(to.x, to.y, IRMath::min(to.z, field.topPlane(toX, toY))),
        IRComponents::kFogLosHardGate,
        bandClearance
    );
    return minClearance >= -IRComponents::kFogLosClearanceTolerance;
}

/// A caller-owned, immutable snapshot of the line-of-sight column field for
/// many point queries against the same occluders: one rasterize per
/// `capture`, then each `visible` is one lattice walk with no ECS lookup and
/// no allocation. A snapshot reflects the occluders, the raster lattice and
/// the field corner at capture time and never changes after; recapture after
/// occluders move, the camera turns or the fog window pans. An empty view
/// (never captured, or reset) answers true, the no-occluder fallback
/// `lineOfSight` uses.
///
/// Copies share the snapshot and stay isolated: `capture` rasterizes in place
/// only while no copy holds the snapshot, and allocates a fresh one otherwise.
/// `visible` may run on any thread; `capture` and `reset` must not race a
/// reader of the same view object, so capture before the fan-out or hand each
/// reader a copy. Cost: 2 MiB per live snapshot.
class LineOfSightView {
  public:
    bool visible(IRMath::vec3 from, IRMath::vec3 to) const {
        if (m_snapshot == nullptr) {
            return true;
        }
        return losPointVisible(
            IRComponents::FogLosColumnField{m_snapshot->columnTops_.data(), m_snapshot->fieldMin_},
            from,
            to
        );
    }

    /// Rasterizations into the current snapshot: 1 after a capture that
    /// allocated, +1 per in-place recapture, 0 for an empty view.
    std::uint64_t rasterizeCount() const {
        return m_snapshot == nullptr ? 0u : m_snapshot->rasterizeCount_;
    }

    /// Rasterize @p pool and @p canvas's flagged shapes under @p frame into
    /// the field whose lower corner is @p fieldMin (`rasterizeLosColumns`).
    void capture(
        const IRComponents::C_VoxelPool &pool,
        IREntity::EntityId canvas,
        const LosRasterFrame &frame,
        IRMath::ivec2 fieldMin
    ) {
        if (m_snapshot == nullptr || m_snapshot.use_count() != 1) {
            m_snapshot = std::make_shared<Snapshot>();
            m_snapshot->columnTops_.resize(IRComponents::kFogLosFieldFloatCount);
        }
        rasterizeLosColumns(pool, canvas, frame, fieldMin, m_snapshot->columnTops_);
        m_snapshot->fieldMin_ = fieldMin;
        ++m_snapshot->rasterizeCount_;
    }

    void reset() {
        m_snapshot.reset();
    }

  private:
    struct Snapshot {
        std::vector<float> columnTops_;
        IRMath::ivec2 fieldMin_{0};
        std::uint64_t rasterizeCount_ = 0;
    };
    std::shared_ptr<Snapshot> m_snapshot;
};

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_LINE_OF_SIGHT_H */
