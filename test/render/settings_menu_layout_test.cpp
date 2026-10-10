#include <gtest/gtest.h>

#include <irreden/render/settings_menu_layout.hpp>

#include <string_view>

namespace {

using IRPrefab::GuiRect;
using IRPrefab::SettingsMenu::LayoutInput;
using IRPrefab::SettingsMenu::LayoutRowInput;
using IRPrefab::SettingsMenu::MenuLayout;

constexpr int kDropdownPadding = 4;

LayoutRowInput enumRow(std::string_view name, std::string_view widestItem) {
    return {
        true,
        IRPrefab::GuiText::textRunWidth(name, IRPrefab::Widget::detail::kWidgetTextFontSize),
        IRPrefab::Widget::detail::dropdownMinimumWidth(widestItem, kDropdownPadding)
    };
}

LayoutInput demoInput(IRMath::ivec2 canvasSize) {
    LayoutInput input;
    input.canvasSize_ = canvasSize;
    input.rows_ = {
        enumRow("ROTATION PIVOT", "CAMERA CENTER"),
        enumRow("DEBUG OVERLAY", "PER AXIS ORIGIN"),
        {},
        {},
        {},
    };
    input.hintTextWidth_ = IRPrefab::GuiText::textRunWidth(
        "CONTROLS: F1",
        IRPrefab::Widget::detail::kWidgetTextFontSize
    );
    return input;
}

TEST(SettingsMenuLayoutTest, NarrowPortraitCanvasStacksEnumRowsInsidePanel) {
    const LayoutInput input = demoInput(IRMath::ivec2(271, 961));
    const MenuLayout layout = IRPrefab::SettingsMenu::layoutMenu(input);

    ASSERT_TRUE(layout.enumRowsStacked_);
    EXPECT_EQ(layout.panel_.pos_.x, IRPrefab::SettingsMenu::kMargin);
    EXPECT_EQ(layout.panel_.size_, IRMath::ivec2(239, 304));
    ASSERT_EQ(layout.rows_.size(), input.rows_.size());
    for (std::size_t i = 0; i < layout.rows_.size(); ++i) {
        EXPECT_TRUE(IRPrefab::contains(layout.panel_, layout.rows_[i].control_)) << "row " << i;
        if (!input.rows_[i].hasSeparateLabel_) {
            continue;
        }
        EXPECT_TRUE(IRPrefab::contains(layout.panel_, layout.rows_[i].label_)) << "row " << i;
        EXPECT_FALSE(IRPrefab::intersects(layout.rows_[i].label_, layout.rows_[i].control_))
            << "row " << i;
        EXPECT_GE(layout.rows_[i].control_.size_.x, input.rows_[i].controlMinWidth_) << "row " << i;
    }
    EXPECT_TRUE(IRPrefab::contains(layout.panel_, layout.hint_));
    EXPECT_TRUE(IRPrefab::contains(layout.panel_, layout.quit_));
    EXPECT_GE(layout.panel_.pos_.x, IRPrefab::SettingsMenu::kMargin);
    EXPECT_GE(layout.panel_.pos_.y, IRPrefab::SettingsMenu::kMargin);
    EXPECT_LE(
        layout.panel_.pos_.x + layout.panel_.size_.x,
        input.canvasSize_.x - IRPrefab::SettingsMenu::kMargin
    );
}

TEST(SettingsMenuLayoutTest, WidePortraitCanvasKeepsExistingSideBySideGeometry) {
    const MenuLayout layout =
        IRPrefab::SettingsMenu::layoutMenu(demoInput(IRMath::ivec2(542, 1922)));

    ASSERT_FALSE(layout.enumRowsStacked_);
    EXPECT_EQ(layout.panel_.size_, IRMath::ivec2(380, 256));
    ASSERT_EQ(layout.rows_.size(), 5u);
    const int contentX = layout.panel_.pos_.x + IRPrefab::SettingsMenu::kPad;
    EXPECT_EQ(layout.rows_[0].labelOrigin_.x, contentX);
    EXPECT_EQ(layout.rows_[0].control_.pos_.x, contentX + 160);
    EXPECT_EQ(layout.rows_[0].control_.size_.x, 196);
    EXPECT_EQ(
        layout.rows_[1].control_.pos_.y - layout.rows_[0].control_.pos_.y,
        IRPrefab::SettingsMenu::kRowHeight + IRPrefab::SettingsMenu::kRowGap
    );
}

TEST(SettingsMenuLayoutTest, SideBySideBoundaryReservesTextParityAndChevronGap) {
    const MenuLayout fits = IRPrefab::SettingsMenu::layoutMenu(demoInput(IRMath::ivec2(323, 961)));
    const MenuLayout stacks =
        IRPrefab::SettingsMenu::layoutMenu(demoInput(IRMath::ivec2(322, 961)));

    EXPECT_FALSE(fits.enumRowsStacked_);
    EXPECT_TRUE(stacks.enumRowsStacked_);
    EXPECT_EQ(
        IRPrefab::Widget::detail::dropdownMinimumWidth("PER AXIS ORIGIN", kDropdownPadding),
        145
    );
}

} // namespace
