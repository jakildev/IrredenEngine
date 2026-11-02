#include "../../creations/editors/voxel_editor/recipe_reapply.hpp"
#include "../../creations/editors/voxel_editor/session_builder.hpp"

#include <gtest/gtest.h>

namespace {

using namespace IRVoxelEditor;
using IRComponents::C_Voxel;
const IRMath::Color red{220, 20, 30, 255};
const IRMath::Color blue{20, 30, 220, 255};
const IRMath::ivec3 size{4, 1, 1};

std::vector<C_Voxel> emptyCells() {
    return std::vector<C_Voxel>(4, C_Voxel{IRMath::Color{0, 0, 0, 0}});
}

RecipeCell cell(int x, IRMath::Color color = red) {
    return {IRMath::ivec3{x, 0, 0}, color};
}

TEST(VoxelEditorRecipeReapply, ShrinksAndGrowsTheGeneratedFootprintWithoutTouchingOtherCells) {
    auto live = emptyCells();
    live[0] = C_Voxel{red};
    live[1] = C_Voxel{red};
    live[3] = C_Voxel{blue};
    const std::vector<RecipeCell> before{cell(0), cell(1)};
    const std::vector<RecipeCell> after{cell(1), cell(2)};
    const auto edits = planRecipeReapply(before, after, live, size, red, red);
    ASSERT_TRUE(edits);
    ASSERT_EQ(edits->size(), 2u);
    EXPECT_EQ((*edits)[0].flat_, 0u);
    EXPECT_FALSE((*edits)[0].place_);
    EXPECT_EQ((*edits)[1].flat_, 2u);
    EXPECT_TRUE((*edits)[1].place_);
}

TEST(VoxelEditorRecipeReapply, PreservesPaintErasureAndNewFootprintCollisions) {
    auto live = emptyCells();
    live[0] = C_Voxel{blue};
    live[2] = C_Voxel{blue};
    const std::vector<RecipeCell> before{cell(0), cell(1)};
    const std::vector<RecipeCell> after{cell(0), cell(1), cell(2)};
    const auto edits = planRecipeReapply(before, after, live, size, red, red);
    ASSERT_TRUE(edits);
    EXPECT_TRUE(edits->empty());
    const auto shrunk = planRecipeReapply(before, {}, live, size, red, red);
    ASSERT_TRUE(shrunk);
    EXPECT_TRUE(shrunk->empty());
}

TEST(VoxelEditorRecipeReapply, PreservesAuthoredAttributesButIgnoresDerivedRenderBits) {
    C_Voxel voxel{red};
    voxel.flags_ |= IRComponents::VoxelFlags::kFaceOccludedMask;
    voxel.reserved_ =
        IRComponents::VoxelReserved::kRotatedEmit | IRComponents::VoxelReserved::kFogCarrierMask;
    EXPECT_TRUE(matchesGeneratedRecipeCell(voxel, red));
    const auto baseline = voxel;
    voxel.material_id_ = 1;
    EXPECT_FALSE(matchesGeneratedRecipeCell(voxel, red));
    voxel = baseline;
    voxel.bone_id_ = 1;
    EXPECT_FALSE(matchesGeneratedRecipeCell(voxel, red));
    voxel = baseline;
    voxel.layer_id_ = 1;
    EXPECT_FALSE(matchesGeneratedRecipeCell(voxel, red));
    voxel = baseline;
    voxel.flags_ |= IRComponents::VoxelFlags::kEmissive;
    EXPECT_FALSE(matchesGeneratedRecipeCell(voxel, red));
    voxel = baseline;
    voxel.reserved_ |= 1;
    EXPECT_FALSE(matchesGeneratedRecipeCell(voxel, red));
}

TEST(VoxelEditorRecipeReapply, ResolvesFallbackAndRepeatedCoordinatesLikeApply) {
    auto live = emptyCells();
    live[0] = C_Voxel{blue};
    const std::vector<RecipeCell> before{cell(0), cell(0, blue)};
    const std::vector<RecipeCell> after{{IRMath::ivec3(0), std::nullopt}};
    const auto edits = planRecipeReapply(before, after, live, size, blue, red);
    ASSERT_TRUE(edits);
    ASSERT_EQ(edits->size(), 1u);
    EXPECT_TRUE((*edits)[0].place_);
    EXPECT_EQ((*edits)[0].color_.toPackedRGBA(), red.toPackedRGBA());
}

TEST(VoxelEditorRecipeReapply, UsesMaskedOutputAndRejectsInvalidTargetStorage) {
    auto live = emptyCells();
    live[0] = C_Voxel{blue};
    // Cell 1 is omitted by a hole mask; cell 0 has a class colour override.
    const std::vector<RecipeCell> masked{cell(0, blue), cell(8)};
    const auto edits = planRecipeReapply(masked, masked, live, size, red, red);
    ASSERT_TRUE(edits);
    EXPECT_TRUE(edits->empty());
    EXPECT_FALSE(planRecipeReapply(masked, masked, {}, size, red, red));
    EXPECT_FALSE(planRecipeReapply(masked, masked, live, IRMath::ivec3(0), red, red));
}

TEST(VoxelEditorRecipeReapply, ErasureOutsideTheOldFootprintHasNoPersistentTombstone) {
    const auto live = emptyCells();
    const std::vector<RecipeCell> expanded{cell(1)};
    const auto edits = planRecipeReapply({}, expanded, live, size, red, red);
    ASSERT_TRUE(edits);
    ASSERT_EQ(edits->size(), 1u);
    EXPECT_TRUE((*edits)[0].place_);
}

TEST(VoxelEditorRecipeReapply, BuilderReplacesFootprintAndKeepsManualErasure) {
    ModuleSessionView view;
    view.recipes_.push_back({"column", {{"height", RecipeParamType::INT, 4, 1, 6}}, {}, {}});
    view.evaluate_ = [](std::size_t,
                        const std::vector<float> &values,
                        IRMath::ivec3 extent,
                        const std::vector<RecipeClassMaskEntry> &,
                        std::vector<RecipeCell> &out,
                        std::string &) {
        for (int z = extent.z - 1; z >= extent.z - static_cast<int>(values[0]); --z)
            out.push_back({{extent.x / 2, extent.y / 2, z}, red});
        return true;
    };
    const IRMath::ivec3 extent(8);
    Session::Builder builder("reapply", extent, deriveSceneOrigin(extent), &view);
    builder.addVoxelPart();
    builder.applyRecipe("column", {{"height", 4}});
    EXPECT_TRUE(builder.model().occupied({4, 4, 4}));
    builder.applyRecipe("column", {{"height", 2}});
    EXPECT_FALSE(builder.model().occupied({4, 4, 4}));
    EXPECT_TRUE(builder.model().occupied({4, 4, 6}));
    builder.applyRecipe("column", {{"height", 4}});
    EXPECT_TRUE(builder.model().occupied({4, 4, 4}));
    builder.toggleEraseMode();
    builder.click({4, 4, 4});
    builder.toggleEraseMode();
    builder.applyRecipe("column", {{"height", 5}});
    EXPECT_FALSE(builder.model().occupied({4, 4, 4}));
    EXPECT_TRUE(builder.model().occupied({4, 4, 3}));
    const auto session = builder.finish();
    EXPECT_TRUE(session.ok()) << (session.errors_.empty() ? "" : session.errors_.front());
}

} // namespace
