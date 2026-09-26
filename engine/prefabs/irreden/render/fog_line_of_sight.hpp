#ifndef IR_PREFAB_FOG_LINE_OF_SIGHT_H
#define IR_PREFAB_FOG_LINE_OF_SIGHT_H

// The fog line-of-sight model's CPU half: the column-field raster, its
// pyramid and the exact segment march every consumer shares. The model itself (occluder set,
// eye, gate, sample mapping) is stated once, in
// `component_canvas_fog_of_war.hpp`. `FOG_LOS_BUILD`, the reveal oracle and
// `IRPrefab::Fog::lineOfSight` all reach the rule through `traceLosClearance`,
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
#include <span>

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

/// Lower the top planes of every half-cell in `[minXY, maxXY)` to
/// @p topPlane (the smallest Z is the highest). Out-of-field cells are
/// dropped.
inline void
stampLosBox(std::span<float> columnTops, IRMath::vec2 minXY, IRMath::vec2 maxXY, float topPlane) {
    using IRComponents::FogLosColumnField;
    using IRComponents::kFogLosFieldHalfExtent;
    const int xBegin = IRMath::max(losHalfCellEdge(minXY.x), -kFogLosFieldHalfExtent);
    const int xEnd = IRMath::min(losHalfCellEdge(maxXY.x), kFogLosFieldHalfExtent);
    const int yBegin = IRMath::max(losHalfCellEdge(minXY.y), -kFogLosFieldHalfExtent);
    const int yEnd = IRMath::min(losHalfCellEdge(maxXY.y), kFogLosFieldHalfExtent);
    for (int y = yBegin; y < yEnd; ++y) {
        for (int x = xBegin; x < xEnd; ++x) {
            float &top = columnTops[FogLosColumnField::columnIndex(x, y)];
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

/// Rebuild @p columnTops from @p pool's occluding voxels and every
/// `blocksLOS_` shape on @p canvas (a shape whose `canvasEntity_` is unset
/// belongs to the active canvas, which the caller passes), each at the box
/// the raster draws it as under @p frame, then its pyramid. Cost: one pass
/// over the live voxels (four half-cells each) plus one SDF pass per flagged
/// shape's footprint, plus a third of the field again for the pyramid.
inline void rasterizeLosColumns(
    const IRComponents::C_VoxelPool &pool,
    IREntity::EntityId canvas,
    const LosRasterFrame &frame,
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
        stampLosBox(columnTops, IRMath::vec2(boxMin), IRMath::vec2(boxMax), boxMin.z);
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

/// The smallest clearance the segment from @p eye to @p target keeps above
/// any column it crosses (the header comment of
/// `component_canvas_fog_of_war.hpp` has the rule); positive means the
/// segment passes above every column, `kFogLosColumnEmpty` when it crosses
/// none. The eye's own half-cell never counts. Only columns whose top plane
/// lies strictly above @p target's height enter @p bandClearance, the value
/// @p softness grades; @p target's own surface and the ground around it
/// therefore never soften a sample that rests on them.
///
/// The walk is hierarchical. At level `L` it stands in the block of `2^L`
/// half-cells holding its position, and the block's highest top against the
/// segment's lowest point over the block bounds every clearance inside it
/// from below. A block whose bound cannot lower the verdict — it is at least
/// `-kFogLosClearanceTolerance`, and at least `min(bandClearance, softness)`
/// when the block holds band columns and the gate is soft — is stepped over
/// whole, and the walk climbs to the coarsest level whose block ahead is new
/// (the crossing lies on its boundary); one that might is entered a level
/// finer. A level-0 cell is evaluated exactly. So the result is exact
/// whenever it is below the tolerance, @p bandClearance whenever it is below
/// @p softness, and each is otherwise at least that threshold: the gate's
/// factor is the flat march's. Mirrors `fogLosTraceClearance` in the shader
/// twins, step for step.
inline float traceLosClearance(
    const IRComponents::FogLosColumnField &field,
    IRMath::vec3 eye,
    IRMath::vec3 target,
    float softness,
    float &bandClearance
) {
    using IRComponents::FogLosColumnField;
    using IRComponents::kFogLosClearanceTolerance;
    using IRComponents::kFogLosColumnEmpty;
    constexpr float kCells = static_cast<float>(IRComponents::kFogLosCellsPerUnit);
    constexpr float kNever = kFogLosColumnEmpty;
    constexpr int kMaxLevel = IRComponents::kFogLosLevelCount - 1;
    const IRMath::vec2 start = IRMath::vec2(eye) * kCells;
    const IRMath::vec2 delta = IRMath::vec2(target) * kCells - start;
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
    const float rise = target.z - eye.z;
    const float bandHorizon = target.z - kFogLosClearanceTolerance;
    float minClearance = kNever;
    bandClearance = kNever;
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
            const float tLow = rise > 0.0f ? tExit : t;
            const float clearance = top - (eye.z + tLow * rise);
            const bool inBand = top < bandHorizon;
            if (level == 0) {
                minClearance = IRMath::min(minClearance, clearance);
                if (inBand) {
                    bandClearance = IRMath::min(bandClearance, clearance);
                }
                if (clearance < -kFogLosClearanceTolerance) {
                    return minClearance;
                }
            } else {
                const float needed = (inBand && softness > 0.0f)
                                         ? IRMath::min(bandClearance, softness)
                                         : -kFogLosClearanceTolerance;
                if (clearance < needed) {
                    --level;
                    continue;
                }
            }
        }
        if (tExit >= 1.0f) {
            break;
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

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_LINE_OF_SIGHT_H */
