#ifndef IR_VOXEL_EDITOR_ARRAY_PREVIEW_H
#define IR_VOXEL_EDITOR_ARRAY_PREVIEW_H

#include "array_placement.hpp"
#include "entity_scene.hpp"

#include <irreden/render/depth_tested_primitives.hpp>
#include <irreden/voxel/sdf_bake.hpp>

#include <array>
#include <span>
#include <optional>
#include <vector>

namespace IRVoxelEditor {

// CPU-owned render geometry only: a preview never enters EntityScene, the
// voxel pool, picking, shadows, or persistence. Rebuild on proposal changes;
// append only projects/culls the retained surface for the current camera.
class ArrayPreview {
  public:
    static constexpr std::size_t kMaxSourceCells = 262144;
    static constexpr std::size_t kMaxWalkCells = 2000000;
    static constexpr std::size_t kMaxSurfaceFaces = 200000;

    void clear() {
        m_faces.clear();
        m_guides.clear();
        m_copyCount = 0;
        m_cellCount = 0;
        m_sampledShape = false;
        m_authoredTransformPreview = false;
    }
    int copyCount() const {
        return m_copyCount;
    }
    std::size_t cellCount() const {
        return m_cellCount;
    }
    std::size_t faceCount() const {
        return m_faces.size();
    }
    bool sampledShape() const {
        return m_sampledShape;
    }
    bool authoredTransformPreview() const {
        return m_authoredTransformPreview;
    }

    EntitySceneResult rebuild(
        const EntityScene &scene,
        const std::vector<IRComponents::C_LocalTransform> &offsets,
        bool radial,
        float radius
    ) {
        clear();
        const auto *part = scene.selectedPart();
        if (!part)
            return {false, "Select a part to preview copies."};
        const auto parentId =
            part->parentId_.empty() ? scene.root() : scene.findPart(part->parentId_)->entity_;
        const auto parent = IREntity::getComponent<IRComponents::C_WorldTransform>(parentId);
        const auto local = IREntity::getComponent<IRComponents::C_LocalTransform>(part->entity_);
        if (part->kind_ == EditorPartKind::VOXEL_SET) {
            const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(part->entity_);
            return rebuildVoxels(
                set.authoredRecords(),
                set.size_,
                set.anchor_,
                local,
                parent,
                offsets,
                part->mode_,
                radial,
                radius,
                voxelSetLocalOrigin(set)
            );
        }
        const auto shape = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(part->entity_);
        const auto geometryFlags = IRMath::SDF::SHAPE_FLAG_HOLLOW |
                                   IRMath::SDF::SHAPE_FLAG_MIRROR_X |
                                   IRMath::SDF::SHAPE_FLAG_MIRROR_Y;
        if ((shape.flags_ & geometryFlags) != 0)
            return {false, "Voxel preview does not support hollow or mirrored shapes."};
        const auto half = IRMath::SDF::boundingHalf(shape.shapeType_, shape.params_);
        for (int axis = 0; axis < 3; ++axis)
            if (!IRMath::isFinite(half[axis]) || half[axis] < 0 || half[axis] > 127)
                return {false, "Shape is too large for a voxel preview."};
        // Odd bounds keep the sampled cell centers on the shape's integer lattice.
        const auto size = IRMath::ivec3(IRMath::ceil(half)) * 2 + IRMath::ivec3(3);
        const auto dense = IRPrefab::Voxel::bakeSdfGrid(
            size,
            shape.shapeType_,
            shape.params_,
            shape.color_,
            kMaxSourceCells * (sizeof(IRAsset::VoxelRecord) + sizeof(float))
        );
        if (dense.voxels_.empty())
            return {false, "Shape has no supported voxel preview."};
        const auto voxels = IRPrefab::DenseVoxel::toVoxels(dense);
        auto result = rebuildVoxels(
            voxels,
            size,
            IRComponents::EntityAnchor::CENTER,
            local,
            parent,
            offsets,
            IRComponents::RotationMode::DETACHED,
            radial,
            radius
        );
        m_sampledShape = result.ok_;
        return result;
    }

    EntitySceneResult rebuildVoxels(
        std::span<const IRComponents::C_Voxel> source,
        IRMath::ivec3 size,
        IRComponents::EntityAnchor anchor,
        const IRComponents::C_LocalTransform &local,
        const IRComponents::C_WorldTransform &parent,
        const std::vector<IRComponents::C_LocalTransform> &offsets,
        IRComponents::RotationMode mode,
        bool radial = false,
        float radius = 0.0f,
        std::optional<IRMath::vec3> sourceOrigin = std::nullopt
    ) {
        clear();
        const auto count = boundedVolume(size, kMaxSourceCells);
        if (count == 0 || source.size() < count)
            return {false, "Source is too large or unavailable for preview."};
        if (offsets.empty())
            return {false, "Choose at least one copy."};
        const auto origin = sourceOrigin.value_or(IRComponents::anchorOffset(anchor, size));
        std::size_t remainingWalk = kMaxWalkCells;
        for (const auto &offset : offsets) {
            const auto world = arrayCopyWorldTransform(local, offset, parent);
            if (!validTransform(world))
                return fail("Preview requires finite, nonzero transform scales.");
            if (mode == IRComponents::RotationMode::GRID &&
                !IRPrefab::GridRotation::isIdentityTransform(world)) {
                if (!appendResampled(source, size, origin, world, remainingWalk))
                    return fail("Array exceeds the preview geometry budget.");
            } else {
                if (count > remainingWalk)
                    return fail("Array exceeds the preview geometry budget.");
                remainingWalk -= count;
                if (!appendVolume(source, size, origin, world))
                    return fail("Array exceeds the preview geometry budget.");
            }
            ++m_copyCount;
        }
        if (m_cellCount == 0)
            return fail("The selected part has no visible cells to repeat.");
        m_authoredTransformPreview = mode != IRComponents::RotationMode::GRID;
        appendGuides(local, parent, offsets, radial, radius);
        return {true, {}};
    }

    void append(
        float yaw,
        std::vector<IRPrefab::WorldPrimitiveVertex> &triangles,
        std::vector<IRPrefab::WorldPrimitiveVertex> &lines
    ) const {
        const auto ray = IRMath::isoPixelToPos3DYawed(IRMath::vec2(0), 3.0f, yaw);
        for (const auto &face : m_faces) {
            if (IRMath::dot(face.normal_, ray) >= 0)
                continue;
            for (const int corner : {0, 1, 2, 0, 2, 3})
                triangles.push_back({face.corners_[corner], face.color_});
        }
        lines.insert(lines.end(), m_guides.begin(), m_guides.end());
    }

  private:
    struct Face {
        std::array<IRMath::vec3, 4> corners_;
        IRMath::vec3 normal_;
        IRMath::vec4 color_;
    };
    std::vector<Face> m_faces;
    std::vector<IRPrefab::WorldPrimitiveVertex> m_guides;
    std::vector<IRComponents::C_Voxel> m_resampled;
    int m_copyCount = 0;
    std::size_t m_cellCount = 0;
    bool m_sampledShape = false;
    bool m_authoredTransformPreview = false;

    EntitySceneResult fail(const char *message) {
        clear();
        return {false, message};
    }
    static std::size_t boundedVolume(IRMath::ivec3 size, std::size_t limit) {
        std::size_t count = 1;
        for (int axis = 0; axis < 3; ++axis) {
            if (size[axis] <= 0 || static_cast<std::size_t>(size[axis]) > limit / count)
                return 0;
            count *= static_cast<std::size_t>(size[axis]);
        }
        return count;
    }
    static IRMath::ivec3 cellAt(std::size_t index, IRMath::ivec3 size) {
        const auto x = index % static_cast<std::size_t>(size.x);
        index /= static_cast<std::size_t>(size.x);
        return {
            static_cast<int>(x),
            static_cast<int>(index % size.y),
            static_cast<int>(index / size.y)
        };
    }
    static bool inside(IRMath::ivec3 cell, IRMath::ivec3 size) {
        return cell.x >= 0 && cell.y >= 0 && cell.z >= 0 && cell.x < size.x && cell.y < size.y &&
               cell.z < size.z;
    }
    static IRMath::vec3 point(IRMath::vec3 local, const IRComponents::C_WorldTransform &world) {
        return world.translation_ +
               IRMath::rotateVectorByQuat(world.scale_ * local, world.rotation_);
    }
    static bool validTransform(const IRComponents::C_WorldTransform &world) {
        for (int axis = 0; axis < 3; ++axis)
            if (!IRMath::isFinite(world.translation_[axis]) ||
                !IRMath::isFinite(world.scale_[axis]) || world.scale_[axis] == 0)
                return false;
        for (int axis = 0; axis < 4; ++axis)
            if (!IRMath::isFinite(world.rotation_[axis]))
                return false;
        return true;
    }
    bool appendVolume(
        std::span<const IRComponents::C_Voxel> source,
        IRMath::ivec3 size,
        IRMath::vec3 origin,
        const IRComponents::C_WorldTransform &world
    ) {
        const auto count = boundedVolume(size, kMaxWalkCells);
        for (std::size_t index = 0; index < count; ++index) {
            const auto color = source[index].color_;
            if (color.alpha_ == 0)
                continue;
            ++m_cellCount;
            const auto cell = cellAt(index, size);
            for (int axis = 0; axis < 3; ++axis) {
                for (int sign : {-1, 1}) {
                    auto neighbor = cell;
                    neighbor[axis] += sign;
                    if (inside(neighbor, size) &&
                        source[IRMath::index3DtoIndex1D(neighbor, size)].color_.alpha_ != 0)
                        continue;
                    if (m_faces.size() >= kMaxSurfaceFaces)
                        return false;
                    const int u = (axis + 1) % 3;
                    const int v = (axis + 2) % 3;
                    auto center = IRMath::vec3(cell) + origin;
                    center[axis] += static_cast<float>(sign) * 0.5f;
                    Face face{};
                    constexpr std::array<IRMath::vec2, 4> corners{
                        IRMath::vec2(-0.5f, -0.5f),
                        IRMath::vec2(0.5f, -0.5f),
                        IRMath::vec2(0.5f, 0.5f),
                        IRMath::vec2(-0.5f, 0.5f)
                    };
                    for (int corner = 0; corner < 4; ++corner) {
                        auto local = center;
                        local[u] += corners[corner].x;
                        local[v] += corners[corner].y;
                        face.corners_[corner] = point(local, world);
                    }
                    IRMath::vec3 normal(0);
                    normal[axis] = static_cast<float>(sign) / world.scale_[axis];
                    face.normal_ = IRMath::rotateVectorByQuat(normal, world.rotation_);
                    const IRMath::vec3 rgb(color.red_, color.green_, color.blue_);
                    face.color_ =
                        IRMath::vec4(rgb / 255.0f * 0.6f + IRMath::vec3(0.1f, 0.3f, 0.4f), 0.32f);
                    m_faces.push_back(face);
                }
            }
        }
        return true;
    }

    bool appendResampled(
        std::span<const IRComponents::C_Voxel> source,
        IRMath::ivec3 size,
        IRMath::vec3 origin,
        const IRComponents::C_WorldTransform &world,
        std::size_t &remainingWalk
    ) {
        // GRID uses a rounded source lattice, including half-integer anchors.
        const auto sourceOrigin = IRMath::roundVec3HalfUp(origin);
        const auto lo = IRMath::vec3(sourceOrigin) - IRMath::vec3(0.5f);
        const auto hi = IRMath::vec3(sourceOrigin + size) - IRMath::vec3(0.5f);
        IRMath::vec3 minimum = point(lo, world), maximum = minimum;
        for (int corner = 0; corner < 8; ++corner) {
            const auto transformed = point(
                {(corner & 1) ? hi.x : lo.x,
                 (corner & 2) ? hi.y : lo.y,
                 (corner & 4) ? hi.z : lo.z},
                world
            );
            minimum = IRMath::min(minimum, transformed);
            maximum = IRMath::max(maximum, transformed);
        }
        for (int axis = 0; axis < 3; ++axis)
            if (!IRMath::isFinite(minimum[axis]) || !IRMath::isFinite(maximum[axis]) ||
                minimum[axis] < -1000000.0f || maximum[axis] > 1000000.0f)
                return false;
        const auto first = IRMath::ivec3(IRMath::floor(minimum));
        const auto dimensions = IRMath::ivec3(IRMath::ceil(maximum)) - first + IRMath::ivec3(1);
        const auto count = boundedVolume(dimensions, remainingWalk);
        if (count == 0)
            return false;
        remainingWalk -= count;
        m_resampled.assign(count, IRComponents::C_Voxel{IRMath::Color{0, 0, 0, 0}});
        const auto inverse = IRMath::quatInverse(world.rotation_);
        for (std::size_t index = 0; index < count; ++index) {
            const auto sourceCell = IRMath::roundVec3HalfUp(
                                        IRPrefab::GridRotation::sourceCellForWorldCell(
                                            first + cellAt(index, dimensions),
                                            world,
                                            inverse
                                        )
                                    ) -
                                    sourceOrigin;
            if (inside(sourceCell, size))
                m_resampled[index] = source[IRMath::index3DtoIndex1D(sourceCell, size)];
        }
        return appendVolume(m_resampled, dimensions, IRMath::vec3(first), {});
    }

    void appendGuides(
        const IRComponents::C_LocalTransform &source,
        const IRComponents::C_WorldTransform &parent,
        const std::vector<IRComponents::C_LocalTransform> &offsets,
        bool radial,
        float radius
    ) {
        constexpr IRMath::vec4 color(0.35f, 0.9f, 1.0f, 0.85f);
        const auto line = [&](IRMath::vec3 a, IRMath::vec3 b) {
            m_guides.push_back({point(a, parent), color});
            m_guides.push_back({point(b, parent), color});
        };
        const auto origin = source.translation_;
        for (int axis = 0; axis < 3; ++axis) {
            IRMath::vec3 arm(0);
            arm[axis] = 0.75f;
            line(origin - arm, origin + arm);
        }
        if (radial) {
            for (int i = 0; i < 64; ++i) {
                const float a = IRMath::kTwoPi * static_cast<float>(i) / 64.0f;
                const float b = IRMath::kTwoPi * static_cast<float>(i + 1) / 64.0f;
                line(
                    origin + IRMath::vec3(IRMath::cos(a), IRMath::sin(a), 0) * radius,
                    origin + IRMath::vec3(IRMath::cos(b), IRMath::sin(b), 0) * radius
                );
            }
        }
        for (const auto &offset : offsets)
            line(origin, origin + offset.translation_);
    }
};

} // namespace IRVoxelEditor

#endif
