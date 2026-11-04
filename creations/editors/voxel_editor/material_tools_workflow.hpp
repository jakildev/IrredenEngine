#ifndef MATERIAL_TOOLS_WORKFLOW_H
#define MATERIAL_TOOLS_WORKFLOW_H

#include "palette.hpp"
#include "sculpt_panel.hpp"
#include "session_builder.hpp"

#include <irreden/voxel/components/component_voxel_set.hpp>

namespace IRVoxelEditor::Session {

inline constexpr IRMath::Color kSampledWallColor{84, 121, 197, 255};

struct MaterialToolCheck {
    const char *label_;
    bool closed_;
    bool filled_;
    IRMath::Color centerColor_;
};

inline constexpr std::array kMaterialToolChecks{
    MaterialToolCheck{"open_bucket", false, false, {}},
    MaterialToolCheck{"brush_closes_ring", true, false, {}},
    MaterialToolCheck{"bucket_fills_interior", true, true, kPaletteColors[4]},
    MaterialToolCheck{"undo_bucket", true, false, {}},
    MaterialToolCheck{"redo_bucket", true, true, kPaletteColors[4]},
    MaterialToolCheck{"picker_samples_wall", true, true, kPaletteColors[4]},
    MaterialToolCheck{"sampled_recolor", true, true, kSampledWallColor},
    MaterialToolCheck{"undo_sampled_recolor", true, true, kPaletteColors[4]},
    MaterialToolCheck{"explicit_recolor", true, true, kPaletteColors[6]},
    MaterialToolCheck{"recolor_empty_floor", true, true, kPaletteColors[6]},
    MaterialToolCheck{"undo_recolor", true, true, kPaletteColors[4]},
    MaterialToolCheck{"undo_fill_again", true, false, {}}
};

inline IRMath::ivec3 materialToolCenter(IRMath::ivec3 size) {
    return {size.x / 2, size.y / 2, size.z - 1};
}

inline void seedMaterialTools(IRComponents::C_VoxelSetNew &set) {
    const auto center = materialToolCenter(set.size_);
    auto records = set.mutableAuthoredRecords();
    if (set.size_.x < 7 || set.size_.y < 7 || set.size_.z < 1)
        return;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (IRMath::max(IRMath::abs(x), IRMath::abs(y)) != 2 || (x == 0 && y == -2))
                continue;
            const auto cell = center + IRMath::ivec3(x, y, 0);
            records[IRMath::index3DtoIndex1D(cell, set.size_)].color_ = kSampledWallColor;
        }
    }
    set.resyncAfterAuthoredEdits();
}

inline bool materialToolColorsMatch(
    const IRComponents::C_VoxelSetNew &set, const MaterialToolCheck &check, std::string &actual
) {
    const auto records = set.authoredRecords();
    const auto center = materialToolCenter(set.size_);
    int wrong = 0;
    int occupied = 0;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto i = static_cast<int>(index);
        const IRMath::ivec3 delta = IRMath::ivec3(
                                        i % set.size_.x,
                                        i / set.size_.x % set.size_.y,
                                        i / (set.size_.x * set.size_.y)
                                    ) -
                                    center;
        const int radius = IRMath::max(IRMath::abs(delta.x), IRMath::abs(delta.y));
        IRMath::Color expected{0, 0, 0, 0};
        if (delta.z == 0 && radius == 2) {
            if (delta != IRMath::ivec3(0, -2, 0))
                expected = kSampledWallColor;
            else if (check.closed_)
                expected = kPaletteColors[0];
        } else if (delta.z == 0 && radius < 2 && check.filled_)
            expected = radius == 0 ? check.centerColor_ : kPaletteColors[4];
        const auto seen = records[index].color_;
        occupied += seen.alpha_ != 0;
        if (seen.alpha_ != expected.alpha_ ||
            (seen.alpha_ != 0 && (seen.red_ != expected.red_ || seen.green_ != expected.green_ ||
                                  seen.blue_ != expected.blue_)))
            ++wrong;
    }
    actual = "occupied=" + std::to_string(occupied) + " wrong_cells=" + std::to_string(wrong);
    return wrong == 0 && occupied == (check.filled_ ? 25 : check.closed_ ? 16 : 15);
}

bool evaluateMaterialToolCheck(const void *context, std::string &actual);

inline Recipe buildMaterialTools(IRMath::ivec3 size, IRMath::vec3 origin) {
    Builder builder("material_tools", size, origin);
    const auto center = origin + IRMath::vec3(materialToolCenter(size));
    const auto floor = center + IRMath::vec3(0, 0, 0.5f);
    const auto surface = center - IRMath::vec3(0, 0, 0.5f);
    const auto click = [&](IRMath::vec3 point) { builder.dragWorld(point, point); };
    const auto tool = [&](int index) { builder.clickGui(sculptButtonCenter(index)); };
    const auto phase = [&](std::size_t i) { builder.segment(kMaterialToolChecks[i].label_, 8.0f); };
    phase(0);
    builder.selectPaletteSwatch(4);
    tool(9);
    click(floor);
    phase(1);
    builder.selectPaletteSwatch(0);
    tool(0);
    click(floor + IRMath::vec3(0, -2, 0));
    phase(2);
    builder.selectPaletteSwatch(4);
    tool(9);
    click(floor);
    phase(3);
    tool(6);
    phase(4);
    tool(7);
    phase(5);
    tool(10);
    click(surface + IRMath::vec3(-2, 0, 0));
    phase(6);
    click(surface);
    phase(7);
    tool(6);
    phase(8);
    builder.selectPaletteSwatch(6);
    tool(2);
    click(surface);
    phase(9);
    click(floor + IRMath::vec3(-3, -3, 0));
    phase(10);
    tool(6);
    phase(11);
    tool(6);
    auto recipe = builder.finish();
    for (std::size_t i = 0; i < recipe.segments_.size(); ++i)
        recipe.segments_[i].assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateMaterialToolCheck,
                &kMaterialToolChecks[i],
                kMaterialToolChecks[i].label_
            )
        );
    return recipe;
}

} // namespace IRVoxelEditor::Session
#endif
