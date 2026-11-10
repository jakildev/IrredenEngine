#ifndef SHAPE_PLACEMENT_H
#define SHAPE_PLACEMENT_H

#include "primitive_shapes.hpp"
#include <irreden/render/depth_tested_primitives.hpp>
#include <irreden/render/picking.hpp>
#include <optional>
#include <vector>

namespace IRVoxelEditor {

struct ShapePlacementProposal {
    PrimitiveShape shape_;
    IRMath::ivec3 size_;
    IRMath::ivec3 lower_;
};

enum class ShapePlacementPhase { HOVER, FOOTPRINT, HEIGHT };

class ShapePlacement {
  public:
    ShapePlacementPhase phase() const {
        return m_phase;
    }
    const std::optional<ShapePlacementProposal> &proposal() const {
        return m_proposal;
    }
    void cancel() {
        m_phase = ShapePlacementPhase::HOVER;
        m_proposal.reset();
    }
    void hover(PrimitiveShape shape, IRMath::ivec3 size, IRMath::ivec3 bottom) {
        m_proposal = ShapePlacementProposal{
            shape,
            size,
            bottom - IRMath::ivec3(size.x / 2, size.y / 2, size.z - 1)
        };
    }
    void beginFootprint(PrimitiveShape shape, IRMath::ivec3 anchor) {
        m_phase = ShapePlacementPhase::FOOTPRINT;
        m_anchor = anchor;
        m_proposal = ShapePlacementProposal{shape, {1, 1, 1}, anchor};
    }
    void sizeFootprint(IRMath::ivec3 target) {
        auto &p = *m_proposal;
        const auto delta = IRMath::ivec2(target - m_anchor);
        if (p.shape_ == PrimitiveShape::CUBE || p.shape_ == PrimitiveShape::PYRAMID) {
            const auto end = IRMath::clamp(delta, IRMath::ivec2(-63), IRMath::ivec2(63));
            p.lower_ = m_anchor + IRMath::ivec3(IRMath::min(end, IRMath::ivec2(0)), 0);
            p.size_ = IRMath::ivec3(IRMath::abs(end) + IRMath::ivec2(1), 1);
        } else {
            const int radius = IRMath::roundHalfUp(IRMath::length(IRMath::vec2(delta)));
            const int side = IRMath::min(64, 2 * radius + 1);
            p.lower_ = m_anchor - IRMath::ivec3(side / 2, side / 2, 0);
            p.size_ = {side, side, 1};
        }
    }
    void finishFootprint(IRMath::vec2 pointer) {
        m_phase = ShapePlacementPhase::HEIGHT;
        m_releasePointer = pointer;
    }
    void finishFootprint(float mouseY) {
        finishFootprint(IRMath::vec2(0, mouseY));
    }
    IRMath::vec2 releasePointer() const {
        return m_releasePointer;
    }
    float releaseY() const {
        return m_releasePointer.y;
    }
    void sizeHeight(int zDelta) {
        auto &p = *m_proposal;
        p.size_.z = IRMath::clamp(1 - zDelta, 1, 64);
        p.lower_.z = m_anchor.z - p.size_.z + 1;
    }

    // Retain merged exterior faces in part-local coordinates. Cursor movement
    // translates the surface without rebuilding the voxel volume.
    void append(
        IRMath::vec3 ray,
        IRMath::Color color,
        std::vector<IRPrefab::WorldPrimitiveVertex> &out,
        std::vector<IRPrefab::WorldPrimitiveVertex> &lines
    ) {
        if (!m_proposal)
            return;
        const auto &p = *m_proposal;
        if (m_meshShape != p.shape_ || m_meshSize != p.size_)
            rebuild(p.shape_, p.size_);
        const auto rgb = IRMath::vec3(color.red_, color.green_, color.blue_) / 255.0f;
        for (const auto &face : m_faces) {
            if (ray[face.axis_] * face.sign_ >= 0)
                continue;
            const float shade = face.axis_ == 2 ? 1.0f : face.axis_ == 1 ? 0.78f : 0.65f;
            const IRMath::vec4 tint(IRMath::mix(rgb, IRMath::vec3(1), 0.20f) * shade, 0.48f);
            const auto origin = IRMath::vec3(p.lower_) + face.corner_;
            for (int i : {0, 1, 2, 0, 2, 3}) {
                auto position = origin;
                if (i == 1 || i == 2)
                    position[(face.axis_ + 1) % 3] += face.length_;
                if (i == 2 || i == 3)
                    position[(face.axis_ + 2) % 3] += 1.0f;
                out.push_back({position, tint});
            }
        }
        if (!m_contourRay || *m_contourRay != ray) {
            rebuildContour(ray);
            m_contourRay = ray;
        }
        for (const auto point : m_contour)
            lines.push_back(
                {IRMath::vec3(p.lower_) + point, IRMath::vec4(0.88f, 0.96f, 1.0f, 0.85f)}
            );
    }

    void append(
        float yaw,
        IRMath::Color color,
        std::vector<IRPrefab::WorldPrimitiveVertex> &out,
        std::vector<IRPrefab::WorldPrimitiveVertex> &lines
    ) {
        append(IRMath::isoPixelToPos3DYawed(IRMath::vec2(0), 3.0f, yaw), color, out, lines);
    }

  private:
    struct FaceRun {
        IRMath::vec3 corner_;
        int axis_;
        int sign_;
        float length_;
    };
    ShapePlacementPhase m_phase = ShapePlacementPhase::HOVER;
    IRMath::ivec3 m_anchor{0};
    IRMath::vec2 m_releasePointer{0};
    std::optional<ShapePlacementProposal> m_proposal;
    PrimitiveShape m_meshShape = PrimitiveShape::CUBE;
    IRMath::ivec3 m_meshSize{0};
    std::vector<std::uint8_t> m_cells;
    std::vector<FaceRun> m_faces;
    std::optional<IRMath::vec3> m_contourRay;
    std::vector<IRMath::vec3> m_contour;

    bool occupied(IRMath::ivec3 cell) const {
        return cellInBounds(cell, m_meshSize) &&
               m_cells[IRMath::index3DtoIndex1D(cell, m_meshSize)] != 0;
    }
    // The contour separates a camera-facing face from a turned-away face.
    // A ray through its midpoint removes steps hidden behind the ghost itself,
    // which is translucent and does not write the scene's depth buffer.
    void rebuildContour(IRMath::vec3 ray) {
        m_contour.clear();
        for (const auto &face : m_faces) {
            if (ray[face.axis_] * face.sign_ >= 0)
                continue;
            const int u = (face.axis_ + 1) % 3;
            const int v = (face.axis_ + 2) % 3;
            auto first = IRMath::ivec3(face.corner_ + IRMath::vec3(0.5f));
            if (face.sign_ > 0)
                --first[face.axis_];
            for (const int sideAxis : {u, v}) {
                for (const int side : {-1, 1}) {
                    if (ray[sideAxis] * side < 0)
                        continue;
                    const int along = sideAxis == u ? v : u;
                    const int count = sideAxis == u ? 1 : static_cast<int>(face.length_);
                    auto cell = first;
                    if (sideAxis == u && side > 0)
                        cell[u] += static_cast<int>(face.length_) - 1;
                    IRMath::ivec3 neighbor(0);
                    neighbor[sideAxis] = side;
                    auto start = IRMath::vec3(cell) - IRMath::vec3(0.5f);
                    if (face.sign_ > 0)
                        start[face.axis_] += 1.0f;
                    if (side > 0)
                        start[sideAxis] += 1.0f;
                    auto sample = start;
                    sample[along] += 0.5f;
                    sample[face.axis_] += 0.001f * static_cast<float>(face.sign_);
                    sample[sideAxis] += 0.001f * static_cast<float>(side);
                    bool extending = false;
                    for (int i = 0; i < count; ++i) {
                        bool visible = !occupied(cell + neighbor);
                        if (visible) {
                            const auto hit = IRPrefab::Picking::castGridRay(
                                sample,
                                ray,
                                m_meshSize,
                                [&](IRMath::ivec3 at) { return occupied(at); }
                            );
                            visible = !hit || hit->rayT_ >= -0.001f;
                        }
                        if (visible != extending)
                            m_contour.push_back(start);
                        extending = visible;
                        ++cell[along];
                        start[along] += 1.0f;
                        sample[along] += 1.0f;
                    }
                    if (extending)
                        m_contour.push_back(start);
                }
            }
        }
    }

    void rebuild(PrimitiveShape shape, IRMath::ivec3 size) {
        m_contourRay.reset();
        m_meshShape = shape;
        m_meshSize = size;
        m_faces.clear();
        m_cells.resize(static_cast<std::size_t>(size.x * size.y * size.z));
        IRMath::iterateAABB(IRMath::ivec3(0), size - IRMath::ivec3(1), [&](int x, int y, int z) {
            const IRMath::ivec3 cell{x, y, z};
            m_cells[IRMath::index3DtoIndex1D(cell, size)] = primitiveShapeCell(shape, cell, size);
        });
        for (int axis = 0; axis < 3; ++axis) {
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            for (const int sign : {-1, 1}) {
                IRMath::ivec3 neighbor{0};
                neighbor[axis] = sign;
                IRMath::ivec3 cell{0};
                for (cell[axis] = 0; cell[axis] < size[axis]; ++cell[axis]) {
                    for (cell[v] = 0; cell[v] < size[v]; ++cell[v]) {
                        for (cell[u] = 0; cell[u] < size[u];) {
                            if (!occupied(cell) || occupied(cell + neighbor)) {
                                ++cell[u];
                                continue;
                            }
                            const int first = cell[u];
                            auto corner = IRMath::vec3(cell) - IRMath::vec3(0.5f);
                            if (sign > 0)
                                corner[axis] += 1.0f;
                            do {
                                ++cell[u];
                            } while (cell[u] < size[u] && occupied(cell) &&
                                     !occupied(cell + neighbor));
                            m_faces.push_back(
                                {corner, axis, sign, static_cast<float>(cell[u] - first)}
                            );
                        }
                    }
                }
            }
        }
    }
};
} // namespace IRVoxelEditor
#endif
