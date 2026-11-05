#include <gtest/gtest.h>

#include "../../creations/editors/voxel_editor/workarea_framing.hpp"

#include <array>

namespace {
using IRVoxelEditor::EditorViewRect;

void expectClear(const EditorViewRect &workarea, std::span<const EditorViewRect> panels) {
    for (const auto &panel : panels) {
        const auto gap = IRMath::vec2(IRVoxelEditor::kWorkareaGutter);
        EXPECT_TRUE(
            workarea.max_.x <= panel.min_.x - gap.x || workarea.min_.x >= panel.max_.x + gap.x ||
            workarea.max_.y <= panel.min_.y - gap.y || workarea.min_.y >= panel.max_.y + gap.y
        );
    }
}

TEST(VoxelEditorWorkareaFraming, ModuleColumnMovesFrameIntoUnobstructedRightHandArea) {
    const std::array panels{
        EditorViewRect{{4, 106}, {630, 218}},
        EditorViewRect{{4, 240}, {374, 694}},
        EditorViewRect{{680, 240}, {800, 500}}
    };
    const auto area = IRVoxelEditor::editorWorkarea({1600, 900}, panels);
    ASSERT_TRUE(area);
    EXPECT_EQ(area->min_, IRMath::vec2(812, 106));
    EXPECT_EQ(area->max_, IRMath::vec2(1588, 888));
    expectClear(*area, panels);
}

TEST(VoxelEditorWorkareaFraming, CompactWindowCanUseSpaceBelowDocksAtRightEdge) {
    const std::array panels{
        EditorViewRect{{4, 106}, {630, 300}},
        EditorViewRect{{680, 240}, {800, 400}}
    };
    const auto area = IRVoxelEditor::editorWorkarea({800, 600}, panels);
    ASSERT_TRUE(area);
    EXPECT_GT(area->size().x, 600);
    EXPECT_GE(area->min_.y, 312);
    expectClear(*area, panels);
}

TEST(VoxelEditorWorkareaFraming, OverlappingDocksDoNotCreateFalseGaps) {
    const std::array panels{
        EditorViewRect{{0, 100}, {500, 300}},
        EditorViewRect{{0, 200}, {500, 500}},
        EditorViewRect{{0, 400}, {500, 700}}
    };
    const auto area = IRVoxelEditor::editorWorkarea({1000, 900}, panels);
    ASSERT_TRUE(area);
    EXPECT_EQ(area->min_, IRMath::vec2(512, 106));
    expectClear(*area, panels);
}

TEST(VoxelEditorWorkareaFraming, OffscreenPanelsDoNotReduceAreaAndCoveredCanvasRefusesFocus) {
    const std::array offscreen{EditorViewRect{{1800, 200}, {1900, 700}}};
    const auto area = IRVoxelEditor::editorWorkarea({1600, 900}, offscreen);
    ASSERT_TRUE(area);
    EXPECT_EQ(area->min_, IRMath::vec2(12, 106));
    EXPECT_EQ(area->max_, IRMath::vec2(1588, 888));
    const std::array covered{EditorViewRect{{0, 0}, {1600, 900}}};
    EXPECT_FALSE(IRVoxelEditor::editorWorkarea({1600, 900}, covered));
    EXPECT_FALSE(IRVoxelEditor::editorWorkarea({20, 80}, {}));
}

TEST(VoxelEditorWorkareaFraming, FitsLimitingAxisWithMarginAndHandlesPointBounds) {
    EXPECT_FLOAT_EQ(IRVoxelEditor::editorFitZoom(4.0f, {200, 100}, {800, 600}), 13.6f);
    EXPECT_FLOAT_EQ(IRVoxelEditor::editorFitZoom(4.0f, {100, 200}, {800, 600}), 10.2f);
    EXPECT_FLOAT_EQ(IRVoxelEditor::editorFitZoom(4.0f, {0, 0}, {800, 600}), 2040.0f);
}
} // namespace
