#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>

#include "../../creations/editors/voxel_editor/fill_ghost.hpp"

#include <set>
#include <tuple>

// The fill tool's drag preview is a BOX shape, and
// `IRMath::SDF::forEachInteriorCell` is the engine's definition of the cells a
// shape covers: the preview is right when that walk visits the fill's cells
// and no others.

namespace {

using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;
using IRVoxelEditor::fillGhostBox;
using IRVoxelEditor::FillGhostBox;
using IRVoxelEditor::viewRayCrossesCells;

using CellSet = std::set<std::tuple<int, int, int>>;

struct Fill {
    ivec3 lo_;
    ivec3 hi_;
};

const Fill kOneCell{ivec3(3, 4, 5), ivec3(3, 4, 5)};
const Fill kRow{ivec3(3, 4, 5), ivec3(6, 4, 5)};
const Fill kCube{ivec3(3, 4, 5), ivec3(5, 6, 7)};
const Fill kBelowOrigin{ivec3(-5, -3, -2), ivec3(-4, -3, 0)};

CellSet cellsOf(const Fill &fill) {
    CellSet cells;
    IRMath::iterateAABB(fill.lo_, fill.hi_, [&](int x, int y, int z) { cells.insert({x, y, z}); });
    return cells;
}

// Walks two cells past the fill on every side, so a box one cell too large on
// any axis shows up as extra cells.
CellSet interiorCells(const FillGhostBox &box, const Fill &fill) {
    CellSet cells;
    IRMath::SDF::forEachInteriorCell(
        IRMath::SDF::ShapeType::BOX,
        box.params_,
        box.center_,
        fill.lo_ - ivec3(2),
        fill.hi_ + ivec3(2),
        [&](ivec3 cell) { cells.insert({cell.x, cell.y, cell.z}); }
    );
    return cells;
}

vec3 cellCounts(const Fill &fill) {
    return vec3(fill.hi_ - fill.lo_ + ivec3(1));
}

// Half extents as params, centred half a cell past the middle of the cells.
FillGhostBox halfExtentBox(const Fill &fill) {
    const vec3 halfExtent = cellCounts(fill) * 0.5f;
    return FillGhostBox{vec3(fill.lo_) + halfExtent, vec4(halfExtent, 0.0f)};
}

// Voxel counts as params, with the same half-cell slip in the centre.
FillGhostBox slippedCentreBox(const Fill &fill) {
    const vec3 counts = cellCounts(fill);
    return FillGhostBox{vec3(fill.lo_) + counts * 0.5f, vec4(counts, 0.0f)};
}

TEST(VoxelEditorFillGhost, CoversExactlyTheCellsOfTheFill) {
    for (const Fill &fill : {kOneCell, kRow, kCube, kBelowOrigin}) {
        const CellSet covered = interiorCells(fillGhostBox(fill.lo_, fill.hi_), fill);
        EXPECT_EQ(covered, cellsOf(fill))
            << "fill lo=(" << fill.lo_.x << "," << fill.lo_.y << "," << fill.lo_.z << ") hi=("
            << fill.hi_.x << "," << fill.hi_.y << "," << fill.hi_.z << ")";
    }
}

TEST(VoxelEditorFillGhost, ParamsAreVoxelCounts) {
    EXPECT_EQ(fillGhostBox(kOneCell.lo_, kOneCell.hi_).params_, vec4(1.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_EQ(fillGhostBox(kRow.lo_, kRow.hi_).params_, vec4(4.0f, 1.0f, 1.0f, 0.0f));
    EXPECT_EQ(fillGhostBox(kCube.lo_, kCube.hi_).params_, vec4(3.0f, 3.0f, 3.0f, 0.0f));
}

// An even count of cells has its middle between two of them.
TEST(VoxelEditorFillGhost, EvenExtentCentresOnAHalfCoordinate) {
    EXPECT_EQ(fillGhostBox(kRow.lo_, kRow.hi_).center_, vec3(4.5f, 4.0f, 5.0f));
    EXPECT_EQ(fillGhostBox(kBelowOrigin.lo_, kBelowOrigin.hi_).center_, vec3(-4.5f, -3.0f, -1.0f));
}

// A box sized in half extents covers none of a thin fill and the inner cells
// only of a thick one.
TEST(VoxelEditorFillGhost, HalfExtentSizingMissesTheFill) {
    EXPECT_EQ(interiorCells(halfExtentBox(kOneCell), kOneCell).size(), 0u);
    EXPECT_EQ(interiorCells(halfExtentBox(kRow), kRow).size(), 0u);
    const CellSet cube = interiorCells(halfExtentBox(kCube), kCube);
    EXPECT_EQ(cube.size(), 8u);
    EXPECT_NE(cube, cellsOf(kCube));
}

// The right size on the slipped centre reaches one cell past the fill on +x,
// +y and +z.
TEST(VoxelEditorFillGhost, SlippedCentreCoversALayerOutsideTheFill) {
    EXPECT_EQ(interiorCells(slippedCentreBox(kOneCell), kOneCell).size(), 8u);
    EXPECT_EQ(interiorCells(slippedCentreBox(kRow), kRow).size(), 20u);
    EXPECT_EQ(interiorCells(slippedCentreBox(kCube), kCube).size(), 64u);
    const CellSet row = interiorCells(slippedCentreBox(kRow), kRow);
    EXPECT_TRUE(row.count({kRow.hi_.x + 1, kRow.hi_.y, kRow.hi_.z}) != 0);
    EXPECT_TRUE(row.count({kRow.lo_.x - 1, kRow.lo_.y, kRow.lo_.z}) == 0);
}

TEST(VoxelEditorFillGhost, ViewRayThroughACellOfTheFillCrossesIt) {
    for (const Fill &fill : {kOneCell, kRow, kCube, kBelowOrigin}) {
        IRMath::iterateAABB(fill.lo_, fill.hi_, [&](int x, int y, int z) {
            const vec3 cell(x, y, z);
            EXPECT_TRUE(viewRayCrossesCells(cell, fill.lo_, fill.hi_));
            // Anywhere along the ray is the same ray.
            EXPECT_TRUE(viewRayCrossesCells(cell - vec3(40.0f), fill.lo_, fill.hi_));
            EXPECT_TRUE(viewRayCrossesCells(cell + vec3(40.0f), fill.lo_, fill.hi_));
        });
    }
}

// The view ray's direction projects to nothing on screen, so a point is off the
// box only when it is displaced across the ray. One cell along (1,-1,0) from a
// single cell's centre clears it; 0.4 of a cell does not.
TEST(VoxelEditorFillGhost, ViewRayBesideTheFillMissesIt) {
    const vec3 centre(kOneCell.lo_);
    const vec3 acrossRay(1.0f, -1.0f, 0.0f);
    EXPECT_FALSE(viewRayCrossesCells(centre + acrossRay, kOneCell.lo_, kOneCell.hi_));
    EXPECT_FALSE(viewRayCrossesCells(centre - acrossRay, kOneCell.lo_, kOneCell.hi_));
    EXPECT_FALSE(viewRayCrossesCells(centre + vec3(0.0f, 0.0f, 1.5f), kOneCell.lo_, kOneCell.hi_));
    EXPECT_TRUE(viewRayCrossesCells(centre + acrossRay * 0.4f, kOneCell.lo_, kOneCell.hi_));
    EXPECT_FALSE(viewRayCrossesCells(vec3(kRow.hi_) + acrossRay * 1.1f, kRow.lo_, kRow.hi_));
}

} // namespace
