#ifndef VOXEL_TEXT_H
#define VOXEL_TEXT_H

// World-space titles built from voxel glyphs (`voxel_font.hpp`). Contract and
// rationale: docs/design/voxel-text.md.
//
// A title is one root entity (C_LocalTransform only) with one CHILD_OF letter
// per printable character, each an unmanaged GRID `C_VoxelSetNew` with an
// identity rotation and an integer translation. Every letter is authored
// straight onto the lattice plane that faces the camera at the build-time yaw
// snapped to a multiple of π/4, so it reads upright and unmirrored without a
// rotation; re-facing after the camera turns is a rebuild. Rotating or scaling
// the root resamples every letter through REBUILD_GRID_VOXELS each frame and
// is unsupported.
//
// `create` changes archetypes eagerly: setup work, never inside a system tick.
// The letters follow the root's translation only with PROPAGATE_TRANSFORM
// before UPDATE_VOXEL_SET_CHILDREN in UPDATE.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/voxel_font.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace IRPrefab::VoxelText {

// Bounds that keep a letter's cell box well inside `int` and a single pool
// span: at the maximum scale a letter is 80 columns by 224 rows.
constexpr int kMaxScale = 16;
constexpr int kMaxDepth = 64;

// The line a title's letters stand on. Both lists empty: the default heading,
// which reads rising to the right on screen. `headings_` (radians about world
// +Z, 0 = +X): entry i steps from letter i to letter i + 1, and a short list
// repeats its last entry. `points_`: world points of a polyline that starts
// at the origin; letters past its end continue along the last segment.
struct VoxelTextPath {
    std::vector<float> headings_;
    std::vector<IRMath::vec3> points_;
};

struct VoxelTextOptions {
    // World position of the first letter's bottom-centre cell, rounded half up
    // to a cell.
    IRMath::vec3 origin_ = IRMath::vec3(0.0f);
    IRMath::Color color_ = IRMath::IRColors::kWhite;
    int depth_ = 2;
    int scale_ = 1;
    VoxelTextPath path_;
    // Absent: the main camera's yaw at the call.
    std::optional<float> cameraYaw_;
    // Null: the active canvas.
    IREntity::EntityId canvas_ = IREntity::kNullEntity;
};

namespace detail {

// The build-time camera yaw rounded to a multiple of π/4, as 0..7.
inline int yawOctant(float yaw) {
    const int octant = IRMath::roundHalfUp(yaw / IRMath::kQuarterPi) % 8;
    return octant < 0 ? octant + 8 : octant;
}

inline float snappedYaw(float yaw) {
    return static_cast<float>(yawOctant(yaw)) * IRMath::kQuarterPi;
}

// One lattice step screen-right on the camera-facing plane:
// R_z(+snappedYaw) · (-1, 1, 0) reduced to its lattice step. At yaw 0 it is
// (-1, 1, 0), because the rendered voxel output places +iso.x screen-right.
inline IRMath::ivec3 readingDirection(float yaw) {
    const int octant = yawOctant(yaw);
    const IRMath::ivec3 viewStep =
        octant % 2 == 0 ? IRMath::ivec3(-1, 1, 0) : IRMath::ivec3(-1, 0, 0);
    return IRMath::rotateCardinalZInvI(viewStep, static_cast<IRMath::CardinalIndex>(octant / 2));
}

// One layer further from the camera: R_z(-π/2) · reading direction.
constexpr IRMath::ivec3 depthNormal(IRMath::ivec3 reading) {
    return IRMath::ivec3(reading.y, -reading.x, 0);
}

// The default path's unit heading, R_z(-π/4) · reading direction: world +Y at
// yaw 0.
inline IRMath::vec3 defaultHeading(float yaw) {
    const IRMath::vec3 reading = IRMath::vec3(readingDirection(yaw));
    return IRMath::normalize(IRMath::vec3(reading.x + reading.y, reading.y - reading.x, 0.0f));
}

inline IRMath::vec3 headingDirection(float heading) {
    return IRMath::vec3(IRMath::cos(heading), IRMath::sin(heading), 0.0f);
}

// The projected offset at which the next letter stands: kVoxelGlyphStepX
// glyph columns of |iso.x| at the snapped yaw, or one glyph height plus one
// bitmap row of |iso.y|, whichever a path reaches first.
inline IRMath::vec2 advanceThresholds(float yaw, int scale) {
    const float snapped = snappedYaw(yaw);
    const float columnIso =
        IRMath::abs(IRMath::pos3DtoPos2DIsoYawed(IRMath::vec3(readingDirection(yaw)), snapped).x);
    const float rowIso =
        IRMath::abs(IRMath::pos3DtoPos2DIsoYawed(IRMath::vec3(0.0f, 0.0f, 1.0f), snapped).y);
    const int rows = (IRRender::kVoxelGlyphHeight + 1) * IRRender::kVoxelGlyphRowVoxels;
    return IRMath::vec2(
        static_cast<float>(IRRender::kVoxelGlyphStepX * scale) * columnIso,
        static_cast<float>(rows * scale) * rowIso
    );
}

// Projected rates below this count as zero: a direction along the camera's
// view-depth axis never moves on screen.
constexpr float kMinScreenRate = 1e-4f;

// Whether walking along `direction` moves the projection at the snapped yaw.
inline bool movesOnScreen(IRMath::vec3 direction, float yaw) {
    const IRMath::vec2 rate =
        IRMath::pos3DtoPos2DIsoYawed(IRMath::normalize(direction), snappedYaw(yaw));
    return IRMath::abs(rate.x) > kMinScreenRate || IRMath::abs(rate.y) > kMinScreenRate;
}

// Walks `vertices` (a polyline whose last segment runs on forever) from the
// point `along` units into `segment` to the nearest point whose projected
// offset from the start reaches a threshold on either iso axis, and leaves
// the cursor there.
inline IRMath::vec3 advanceAlong(
    const std::vector<IRMath::vec3> &vertices,
    std::size_t &segment,
    float &along,
    IRMath::vec2 thresholds,
    float yaw
) {
    const auto pointAt = [&](std::size_t index, float distance) {
        const IRMath::vec3 start = vertices[index];
        return start + IRMath::normalize(vertices[index + 1] - start) * distance;
    };
    const IRMath::vec2 anchor = IRMath::pos3DtoPos2DIsoYawed(pointAt(segment, along), yaw);
    while (true) {
        const IRMath::vec3 start = vertices[segment];
        const IRMath::vec3 direction = IRMath::normalize(vertices[segment + 1] - start);
        const IRMath::vec2 offset =
            IRMath::pos3DtoPos2DIsoYawed(start + direction * along, yaw) - anchor;
        const IRMath::vec2 rate = IRMath::pos3DtoPos2DIsoYawed(direction, yaw);
        float step = std::numeric_limits<float>::infinity();
        for (int axis = 0; axis < 2; ++axis) {
            if (IRMath::abs(rate[axis]) > kMinScreenRate) {
                const float target = rate[axis] > 0.0f ? thresholds[axis] : -thresholds[axis];
                step = IRMath::min(step, (target - offset[axis]) / rate[axis]);
            }
        }
        const bool last = segment + 2 == vertices.size();
        IR_ASSERT(
            !last || step != std::numeric_limits<float>::infinity(),
            "VoxelText: the path's last segment runs along the view-depth axis"
        );
        const float remaining = IRMath::length(vertices[segment + 1] - start) - along;
        if (last || step <= remaining) {
            along += step;
            return start + direction * along;
        }
        ++segment;
        along = 0.0f;
    }
}

// A points path as polyline vertices relative to the origin cell, starting at
// the origin, with repeated points dropped. Fewer than two vertices means the
// path never leaves the origin.
inline std::vector<IRMath::vec3>
polylineVertices(const std::vector<IRMath::vec3> &points, IRMath::ivec3 origin) {
    std::vector<IRMath::vec3> vertices{IRMath::vec3(0.0f)};
    for (const IRMath::vec3 point : points) {
        const IRMath::vec3 local = point - IRMath::vec3(origin);
        if (local != vertices.back()) {
            vertices.push_back(local);
        }
    }
    return vertices;
}

// The cell each of `count` letters stands on, relative to the title's origin
// cell. `origin` is that cell in world space; polyline points are world points.
inline std::vector<IRMath::ivec3> letterPoints(
    std::size_t count, const VoxelTextPath &path, IRMath::ivec3 origin, float yaw, int scale
) {
    IR_ASSERT(
        path.headings_.empty() || path.points_.empty(),
        "VoxelText: a path is headings or points, not both"
    );
    std::vector<IRMath::ivec3> cells;
    cells.reserve(count);
    if (count == 0) {
        return cells;
    }
    const float snapped = snappedYaw(yaw);
    const IRMath::vec2 thresholds = advanceThresholds(yaw, scale);
    cells.push_back(IRMath::ivec3(0));

    if (!path.points_.empty()) {
        const std::vector<IRMath::vec3> vertices = polylineVertices(path.points_, origin);
        IR_ASSERT(vertices.size() >= 2, "VoxelText: path points must leave the origin");
        std::size_t segment = 0;
        float along = 0.0f;
        for (std::size_t i = 1; i < count; ++i) {
            cells.push_back(
                IRMath::roundVec3HalfUp(advanceAlong(vertices, segment, along, thresholds, snapped))
            );
        }
        return cells;
    }

    std::vector<IRMath::vec3> ray(2, IRMath::vec3(0.0f));
    for (std::size_t i = 1; i < count; ++i) {
        const IRMath::vec3 heading =
            path.headings_.empty()
                ? defaultHeading(yaw)
                : headingDirection(path.headings_[IRMath::min(i - 1, path.headings_.size() - 1)]);
        ray[1] = ray[0] + heading;
        std::size_t segment = 0;
        float along = 0.0f;
        ray[0] = advanceAlong(ray, segment, along, thresholds, snapped);
        cells.push_back(IRMath::roundVec3HalfUp(ray[0]));
    }
    return cells;
}

// Calls `fn(offset, layer)` for every cell a glyph emits, `offset` relative to
// the letter's path point: the bottom row of the front layer at the middle
// emitted column. Columns step along `reading`, rows up (-z), layers along
// depthNormal(reading); a bitmap pixel emits `scale` columns by
// kVoxelGlyphRowVoxels * `scale` rows per layer.
template <typename Fn>
void forEachGlyphCell(
    const IRRender::VoxelGlyph &glyph, IRMath::ivec3 reading, int depth, int scale, Fn &&fn
) {
    const IRMath::ivec3 normal = depthNormal(reading);
    const int middleColumn = (IRRender::kVoxelGlyphWidth * scale - 1) / 2;
    const int rowsPerPixel = IRRender::kVoxelGlyphRowVoxels * scale;
    for (int row = 0; row < IRRender::kVoxelGlyphHeight; ++row) {
        const int firstEmittedRow = (IRRender::kVoxelGlyphHeight - 1 - row) * rowsPerPixel;
        for (int col = 0; col < IRRender::kVoxelGlyphWidth; ++col) {
            if (!IRRender::voxelGlyphPixel(glyph, col, row)) {
                continue;
            }
            for (int column = col * scale; column < (col + 1) * scale; ++column) {
                for (int emittedRow = firstEmittedRow; emittedRow < firstEmittedRow + rowsPerPixel;
                     ++emittedRow) {
                    for (int layer = 0; layer < depth; ++layer) {
                        fn(reading * (column - middleColumn) + normal * layer -
                               IRMath::ivec3(0, 0, emittedRow),
                           layer);
                    }
                }
            }
        }
    }
}

struct CellBox {
    IRMath::ivec3 min_;
    IRMath::ivec3 size_;
};

// The cell box a letter's cells fill, relative to its path point.
inline CellBox letterBox(IRMath::ivec3 reading, int depth, int scale) {
    const IRMath::ivec3 normal = depthNormal(reading);
    const int middleColumn = (IRRender::kVoxelGlyphWidth * scale - 1) / 2;
    const int lastColumn = IRRender::kVoxelGlyphWidth * scale - 1;
    const int lastRow = IRRender::kVoxelGlyphHeight * IRRender::kVoxelGlyphRowVoxels * scale - 1;
    IRMath::ivec3 low = IRMath::ivec3(0, 0, -lastRow);
    IRMath::ivec3 high = IRMath::ivec3(0);
    for (const int column : {-middleColumn, lastColumn - middleColumn}) {
        for (const int layer : {0, depth - 1}) {
            const IRMath::ivec3 corner = reading * column + normal * layer;
            low = IRMath::min(low, IRMath::ivec3(corner.x, corner.y, low.z));
            high = IRMath::max(high, IRMath::ivec3(corner.x, corner.y, high.z));
        }
    }
    return CellBox{low, high - low + IRMath::ivec3(1)};
}

} // namespace detail

// Builds `text` as a title and returns its root. Every character but a space
// becomes a letter; a space only advances. `text` is a single line.
inline IREntity::EntityId create(std::string_view text, const VoxelTextOptions &options) {
    IR_ASSERT(
        options.depth_ >= 1 && options.depth_ <= kMaxDepth,
        "VoxelText: depth must be in [1, kMaxDepth]"
    );
    IR_ASSERT(
        options.scale_ >= 1 && options.scale_ <= kMaxScale,
        "VoxelText: scale must be in [1, kMaxScale]"
    );
    IR_ASSERT(text.find('\n') == std::string_view::npos, "VoxelText: text must be a single line");

    const float yaw = options.cameraYaw_.has_value() ? *options.cameraYaw_ : Camera::getYaw();
    const IRMath::ivec3 origin = IRMath::roundVec3HalfUp(options.origin_);
    const IRMath::ivec3 reading = detail::readingDirection(yaw);
    const detail::CellBox box = detail::letterBox(reading, options.depth_, options.scale_);
    const std::vector<IRMath::ivec3> points =
        detail::letterPoints(text.size(), options.path_, origin, yaw, options.scale_);

    const IREntity::EntityId root =
        IREntity::createEntity(IRComponents::C_LocalTransform{IRMath::vec3(origin)});
    std::vector<uint8_t> lit;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const IRRender::VoxelGlyph &glyph = *IRRender::getVoxelGlyph(text[i]);
        if (IRRender::voxelGlyphIsBlank(glyph)) {
            continue;
        }
        lit.assign(static_cast<std::size_t>(box.size_.x * box.size_.y * box.size_.z), 0u);
        detail::forEachGlyphCell(
            glyph,
            reading,
            options.depth_,
            options.scale_,
            [&](IRMath::ivec3 offset, int) {
                lit[IRMath::index3DtoIndex1D(offset - box.min_, box.size_)] = 1u;
            }
        );
        IRComponents::C_VoxelSetNew letter{
            box.size_,
            IRMath::Color{0, 0, 0, 0},
            IRComponents::EntityAnchor::CORNER,
            options.canvas_
        };
        letter.editVoxels([&](int index, IRComponents::C_Voxel &voxel, IRMath::vec3) {
            if (lit[static_cast<std::size_t>(index)] != 0u) {
                voxel.color_ = options.color_;
            }
        });
        const IREntity::EntityId child = IREntity::createEntity(
            IRComponents::C_LocalTransform{IRMath::vec3(points[i] + box.min_)},
            letter
        );
        IREntity::setParent(child, root);
    }
    return root;
}

} // namespace IRPrefab::VoxelText

#endif /* VOXEL_TEXT_H */
