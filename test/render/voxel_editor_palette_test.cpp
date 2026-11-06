#include "../../creations/editors/voxel_editor/palette_model.hpp"
#include "../../creations/editors/voxel_editor/palette_panel.hpp"

#include <gtest/gtest.h>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {

using namespace IRVoxelEditor;
using IRMath::Color;

TEST(VoxelEditorPalette, DefaultsKeepSessionIndicesAndEditsDoNotRecolorAuthoredValues) {
    EditorPalette palette;
    ASSERT_EQ(palette.size(), kPaletteCount);
    for (int i = 0; i < kPaletteCount; ++i)
        EXPECT_EQ(palette.color(i).toPackedRGBA(), kPaletteColors[i].toPackedRGBA());
    const IRComponents::C_Voxel authored{palette.color(4)};
    ASSERT_TRUE(palette.edit(4, Color{12, 34, 56, 0}));
    EXPECT_EQ(palette.color(4).toPackedRGBA(), (Color{12, 34, 56, 255}).toPackedRGBA());
    EXPECT_EQ(authored.color_.toPackedRGBA(), kPaletteColors[4].toPackedRGBA());
    EXPECT_FALSE(palette.edit(-1, Color{}));
    EXPECT_FALSE(palette.edit(palette.size(), Color{}));
    palette.resetDefaults();
    EXPECT_EQ(palette.color(4).toPackedRGBA(), kPaletteColors[4].toPackedRGBA());
    EXPECT_EQ(palette.entries()[4].name_, "Green");
}

TEST(VoxelEditorPalette, HexInputUsesExactlySixRgbDigits) {
    for (const auto text : {"#12aBef", "12ABEF"}) {
        const auto color = parsePaletteHex(text);
        ASSERT_TRUE(color);
        EXPECT_EQ(paletteHex(*color), "#12ABEF");
        EXPECT_EQ(color->alpha_, 255);
    }
    for (const auto text :
         {"", "#", "#123", "#12345678", "#12345G", " 123456", "123456 ", "+12345"})
        EXPECT_FALSE(parsePaletteHex(text)) << text;
}

TEST(VoxelEditorPalette, GimpPalettesAcceptNamedAndLegacyRowsWithOpaqueColors) {
    EditorPalette palette;
    ASSERT_TRUE(palette
                    .loadGplText(
                        "GIMP Palette\r\nName: Example\r\nColumns: 4\r\n# note\r\n"
                        "12 34 56 Dark leaf\r\n255 128 0\r\n\r\n"
                    )
                    .ok_);
    ASSERT_EQ(palette.size(), 2);
    EXPECT_EQ(palette.name(), "Example");
    EXPECT_EQ(palette.entries()[0].name_, "Dark leaf");
    EXPECT_EQ(paletteHex(palette.color(0)), "#0C2238");
    EXPECT_EQ(palette.color(1).alpha_, 255);
    ASSERT_TRUE(palette.loadGplText("GIMP Palette\n# legacy\n0 1 2 Sky\n", "legacy").ok_);
    EXPECT_EQ(palette.name(), "legacy");
    EXPECT_EQ(palette.size(), 1);
    EXPECT_EQ(paletteHex(palette.color(0)), "#000102");
}

TEST(VoxelEditorPalette, InvalidLoadNeverReplacesTheCurrentPalette) {
    EditorPalette palette;
    ASSERT_TRUE(palette.edit(0, Color{1, 2, 3, 255}));
    for (const std::string text :
         {"wrong\n1 2 3\n",
          "GIMP Palette\n# empty\n",
          "GIMP Palette\nName: bad\nColumns: 999\n1 2 3\n",
          "GIMP Palette\n1 2 3 valid\n256 4 5 invalid\n",
          "GIMP Palette\n-1 2 3\n",
          "GIMP Palette\n1 2\n",
          "GIMP Palette\n1 2 3name\n"}) {
        const auto result = palette.loadGplText(text);
        EXPECT_FALSE(result.ok_) << text;
        EXPECT_FALSE(result.error_.empty());
        EXPECT_EQ(palette.size(), 16);
        EXPECT_EQ(paletteHex(palette.color(0)), "#010203");
        EXPECT_EQ(palette.name(), "Default");
    }
    std::string tooMany = "GIMP Palette\n";
    for (int i = 0; i <= kPaletteCapacity; ++i)
        tooMany += "1 2 3\n";
    EXPECT_FALSE(palette.loadGplText(tooMany).ok_);
    EXPECT_EQ(paletteHex(palette.color(0)), "#010203");
}

TEST(VoxelEditorPalette, CapacityIsBoundedAndFullPaletteRemainsUsable) {
    EditorPalette palette;
    while (palette.size() < kPaletteCapacity)
        ASSERT_TRUE(palette.add(Color{17, 34, 51, 255}));
    EXPECT_FALSE(palette.add(Color{0, 0, 0, 255}));
    EXPECT_EQ(palette.size(), 256);
    EXPECT_TRUE(palette.edit(255, Color{51, 34, 17, 255}));
    EXPECT_EQ(paletteHex(palette.color(255)), "#332211");
}

class VoxelEditorPaletteFile : public testing::Test {
  protected:
    void SetUp() override {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_directory =
            std::filesystem::temp_directory_path() / ("ir_palette_" + std::to_string(stamp));
        ASSERT_TRUE(std::filesystem::create_directory(m_directory));
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }

    static std::string read(const std::filesystem::path &path) {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::filesystem::path m_directory;
};

TEST_F(VoxelEditorPaletteFile, GplRoundTripKeepsColorsNamesAndSafelyReplacesExistingFile) {
    EditorPalette palette;
    ASSERT_TRUE(palette.loadGplText("GIMP Palette\nName: Forest\nColumns: 4\n12 34 56 Bark\n").ok_);
    const auto file = m_directory / "nested" / "forest.gpl";
    ASSERT_TRUE(palette.saveGpl(file).ok_);
    EditorPalette loaded;
    ASSERT_TRUE(loaded.loadGpl(file).ok_);
    EXPECT_EQ(loaded.name(), "Forest");
    ASSERT_EQ(loaded.size(), 1);
    EXPECT_EQ(loaded.entries()[0].name_, "Bark");
    EXPECT_EQ(paletteHex(loaded.color(0)), "#0C2238");
    ASSERT_TRUE(palette.edit(0, Color{9, 8, 7, 255}));
    ASSERT_TRUE(palette.saveGpl(file).ok_);
    ASSERT_TRUE(loaded.loadGpl(file).ok_);
    EXPECT_EQ(paletteHex(loaded.color(0)), "#090807");
    EXPECT_FALSE(std::filesystem::exists(file.parent_path() / ".forest.gpl.saving"));
}

TEST_F(VoxelEditorPaletteFile, SaveFailurePreservesFileAndLoadFailurePreservesColors) {
    EditorPalette palette;
    const auto file = m_directory / "kept.gpl";
    ASSERT_TRUE(palette.saveGpl(file).ok_);
    const auto original = read(file);
    ASSERT_TRUE(std::filesystem::create_directory(m_directory / ".kept.gpl.saving"));
    ASSERT_TRUE(palette.edit(0, Color{9, 8, 7, 255}));
    EXPECT_FALSE(palette.saveGpl(file).ok_);
    EXPECT_EQ(read(file), original);
    EXPECT_FALSE(palette.loadGpl(m_directory / "missing.gpl").ok_);
    EXPECT_EQ(paletteHex(palette.color(0)), "#090807");
    const auto directoryTarget = m_directory / "folder.gpl";
    ASSERT_TRUE(std::filesystem::create_directory(directoryTarget));
    EXPECT_FALSE(palette.saveGpl(directoryTarget).ok_);
    EXPECT_TRUE(std::filesystem::is_directory(directoryTarget));
}

class VoxelEditorPalettePanel : public testing::Test {
  protected:
    void SetUp() override {
        m_panel.create();
        m_panel.update(m_palette, m_selected);
    }

    IREntity::EntityId input(int index) const {
        for (const auto widget : m_panel.widgets()) {
            if (IREntity::getComponentOptional<IRComponents::C_WidgetTextInput>(widget) &&
                index-- == 0)
                return widget;
        }
        return IREntity::kNullEntity;
    }

    void click(std::string_view name) {
        IREntity::EntityId target = IREntity::kNullEntity;
        for (const auto widget : m_panel.widgets()) {
            const auto button =
                IREntity::getComponentOptional<IRComponents::C_WidgetButton>(widget);
            if (button && button.value()->label_ == name)
                target = widget;
        }
        ASSERT_NE(target, IREntity::kNullEntity);
        IREntity::getComponent<IRComponents::C_WidgetState>(target).fireAction_ = true;
        m_panel.update(m_palette, m_selected);
        IREntity::getComponent<IRComponents::C_WidgetState>(target).fireAction_ = false;
    }

    IREntity::EntityManager m_entities;
    EditorPalette m_palette;
    PalettePanel m_panel;
    int m_selected = 0;
};

#ifndef IR_RELEASE
TEST_F(VoxelEditorPalettePanel, RejectsDockSizesThatWouldClipControls) {
    PalettePanel tooNarrow;
    EXPECT_THROW(tooNarrow.create({4, 112}, {203, 440}), std::runtime_error);
    PalettePanel tooShort;
    EXPECT_THROW(tooShort.create({4, 112}, {204, 439}), std::runtime_error);
}
#endif

TEST_F(VoxelEditorPalettePanel, EditAndAddUseHexAndPageNewColorsWithoutChangingDefaultIndices) {
    IRPrefab::Widget::setTextInputValue(input(0), "#112233");
    click("EDIT");
    EXPECT_EQ(paletteHex(m_palette.color(0)), "#112233");
    EXPECT_EQ(paletteHex(IRPrefab::Widget::colorSwatchColor(m_panel.swatches()[0])), "#112233");
    IRPrefab::Widget::setTextInputValue(input(0), "#AABBCC");
    click("ADD");
    EXPECT_EQ(m_selected, 16);
    ASSERT_EQ(m_palette.size(), 17);
    EXPECT_EQ(paletteHex(m_palette.color(16)), "#AABBCC");
    EXPECT_TRUE(IRPrefab::Widget::colorSwatchSelected(m_panel.swatches()[0]));
    EXPECT_FALSE(IRPrefab::Widget::isVisible(m_panel.swatches()[1]));
    click("PREV");
    EXPECT_TRUE(IRPrefab::Widget::isVisible(m_panel.swatches()[15]));
    const auto swatch = m_panel.swatches()[4];
    IREntity::getComponent<IRComponents::C_WidgetState>(swatch).fireAction_ = true;
    m_panel.update(m_palette, m_selected);
    IREntity::getComponent<IRComponents::C_WidgetState>(swatch).fireAction_ = false;
    EXPECT_EQ(m_selected, 4);
    EXPECT_EQ(m_palette.color(4).toPackedRGBA(), kPaletteColors[4].toPackedRGBA());
    EXPECT_EQ(IRPrefab::Widget::textInputValue(input(0)), paletteHex(kPaletteColors[4]));
}

TEST_F(VoxelEditorPalettePanel, InvalidHexIsVisibleAndDefaultsRestorePaletteAndSelection) {
    IRPrefab::Widget::setTextInputValue(input(0), "#ZZZZZZ");
    click("EDIT");
    EXPECT_EQ(m_panel.status(), "Use six hex digits");
    EXPECT_EQ(m_palette.color(0).toPackedRGBA(), kPaletteColors[0].toPackedRGBA());
    IRPrefab::Widget::setTextInputValue(input(0), "#010203");
    click("ADD");
    click("RESTORE DEFAULTS");
    EXPECT_EQ(m_selected, 0);
    EXPECT_EQ(m_palette.size(), kPaletteCount);
    EXPECT_EQ(m_panel.status(), "Defaults restored");
    EXPECT_TRUE(m_panel.containsCursor(IRMath::vec2(kPalettePanelPos + IRMath::ivec2(10, 10))));
}

TEST_F(VoxelEditorPalettePanel, HiddenSelectionChangesDoNotRevealSwatchesAndShowRestoresThePage) {
    ASSERT_TRUE(m_palette.add(Color{1, 2, 3, 255}));
    IRPrefab::Widget::setVisible(m_panel.panel(), false);
    m_selected = 16;
    m_panel.update(m_palette, m_selected);
    for (const auto swatch : m_panel.swatches())
        EXPECT_FALSE(IRPrefab::Widget::isVisible(swatch));
    IRPrefab::Widget::setVisible(m_panel.panel(), true);
    m_panel.update(m_palette, m_selected);
    EXPECT_TRUE(IRPrefab::Widget::isVisible(m_panel.swatches()[0]));
    EXPECT_TRUE(IRPrefab::Widget::colorSwatchSelected(m_panel.swatches()[0]));
    EXPECT_EQ(paletteHex(IRPrefab::Widget::colorSwatchColor(m_panel.swatches()[0])), "#010203");
    EXPECT_FALSE(IRPrefab::Widget::isVisible(m_panel.swatches()[1]));
}

} // namespace
