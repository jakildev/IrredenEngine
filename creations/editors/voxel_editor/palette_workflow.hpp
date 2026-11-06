#ifndef PALETTE_WORKFLOW_H
#define PALETTE_WORKFLOW_H

#include "palette_model.hpp"
#include "sculpt_panel.hpp"
#include "session_builder.hpp"

#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>

namespace IRVoxelEditor::Session {

inline constexpr std::string_view kPaletteWorkflowPath = "data/palettes/workflow.gpl";
inline constexpr int kPaletteWorkflowShotCount = 8;

inline IRMath::ivec3 paletteWorkflowCell(int index, IRMath::ivec3 size) {
    return {3 + index * 4, 5, size.z - 1};
}

bool evaluatePaletteWorkflowCheck(const void *context, std::string &actual);

// The main adapter supplies the live selected set. Every stage checks all
// authored RGB values, so changing/loading swatches cannot silently recolor it.
inline bool checkPaletteWorkflow(
    int stage,
    const EditorPalette &palette,
    int selected,
    const IRComponents::C_VoxelSetNew &set,
    std::string &actual
) {
    const IRMath::Color edited{18, 52, 86, 255};
    const IRMath::Color added{161, 178, 195, 255};
    const bool defaults = stage == 0 || stage == 5;
    const int count = (stage < 2 || stage == 5) ? 16 : 17;
    const int selection = stage < 2 ? 4 : (stage == 5 || stage == 6 ? 0 : 16);
    bool ok =
        palette.size() == count && selected == selection &&
        palette.color(4).toPackedRGBA() == (defaults ? kPaletteColors[4] : edited).toPackedRGBA();
    if (count == 17)
        ok = ok && palette.color(16).toPackedRGBA() == added.toPackedRGBA();
    const int expectedCells = stage < 3 ? 1 : (stage == 7 ? 3 : 2);
    int occupied = 0;
    const auto records = set.authoredRecords();
    for (const auto &record : records)
        occupied += record.color_.alpha_ != 0;
    ok = ok && occupied == expectedCells;
    for (int index = 0; index < expectedCells; ++index) {
        const auto flat =
            IRMath::index3DtoIndex1D(paletteWorkflowCell(index, set.size_), set.size_);
        ok = ok && flat < records.size() &&
             records[flat].color_.toPackedRGBA() ==
                 (index == 0 ? kPaletteColors[4] : added).toPackedRGBA();
    }
    if (stage == 4) {
        EditorPalette saved;
        const auto result = saved.loadGpl(std::string(kPaletteWorkflowPath));
        ok = ok && result.ok_ && saved.size() == 17 &&
             saved.color(4).toPackedRGBA() == edited.toPackedRGBA() &&
             saved.color(16).toPackedRGBA() == added.toPackedRGBA();
    }
    actual = "stage=" + std::to_string(stage) + " colors=" + std::to_string(palette.size()) +
             " selected=" + std::to_string(selected) + " authored=" + std::to_string(occupied);
    return ok;
}

inline Recipe buildPaletteWorkflow(IRMath::ivec3 size, IRMath::vec3 origin) {
    Builder builder("palette_workflow", size, origin);
    const auto gui = [](IRMath::vec2 relative) {
        return IRMath::vec2(kPalettePanelPos) + relative;
    };
    const auto replace = [&](IRMath::vec2 position, int oldLength, std::string_view value) {
        builder.clickGui(position);
        builder.tapKey(IRInput::kKeyButtonEnd);
        for (int i = 0; i < oldLength; ++i)
            builder.tapKey(IRInput::kKeyButtonBackspace);
        builder.typeText(position, value);
    };
    const auto paint = [&](int index) {
        const auto point = deriveSceneOrigin(size) +
                           IRMath::vec3(paletteWorkflowCell(index, size)) +
                           IRMath::vec3(0, 0, 0.5f);
        builder.dragWorld(point, point);
    };
    builder.segment("default_color_paints");
    builder.addVoxelPart();
    builder.clickGui(sculptButtonCenter(0));
    builder.selectPaletteSwatch(4);
    paint(0);
    builder.segment("edit_does_not_recolor");
    replace(gui({80, 287}), 7, "123456");
    builder.clickGui(gui({54, 311}));
    builder.segment("add_color_second_page");
    replace(gui({80, 287}), 7, "A1B2C3");
    builder.clickGui(gui({150, 311}));
    builder.segment("new_color_paints");
    paint(1);
    builder.segment("save_gpl");
    replace(
        gui({80, 351}),
        std::string_view("data/palettes/custom.gpl").size(),
        kPaletteWorkflowPath
    );
    builder.clickGui(gui({150, 375}));
    builder.segment("defaults_do_not_recolor");
    builder.clickGui(gui({102, 399}));
    builder.segment("load_saved_palette");
    builder.clickGui(gui({54, 375}));
    builder.segment("loaded_color_paints");
    builder.clickGui(gui({174, 245}));
    builder.clickGui(paletteSwatchCenterGuiTrixel(0));
    paint(2);
    auto recipe = builder.finish();
    static constexpr std::array<int, kPaletteWorkflowShotCount> checks{0, 1, 2, 3, 4, 5, 6, 7};
    for (int stage : checks)
        recipe.segments_[stage].assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluatePaletteWorkflowCheck,
                &checks[stage],
                "palette_rgb_persistence_and_native_controls"
            )
        );
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
