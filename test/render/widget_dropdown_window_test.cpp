#include <gtest/gtest.h>

#include <irreden/render/components/component_widget.hpp>

#include <string>

namespace {

IRComponents::C_WidgetDropdown makeDropdown(int count) {
    IRComponents::C_WidgetDropdown dropdown;
    dropdown.itemHeight_ = 10;
    for (int i = 0; i < count; ++i) {
        dropdown.items_.push_back("item " + std::to_string(i));
    }
    return dropdown;
}

constexpr int kHeader = 20;

TEST(WidgetDropdownWindow, LimitsTheStripToEightRowsAndMapsAbsoluteItems) {
    IRComponents::C_WidgetDropdown dropdown = makeDropdown(20);
    EXPECT_EQ(dropdown.visibleRowCount(), IRComponents::kDropdownMaxVisibleRows);
    EXPECT_EQ(dropdown.stripHeight(), 8 * dropdown.rowHeight());
    EXPECT_EQ(dropdown.expandedHeight(kHeader), kHeader + 80);

    dropdown.scrollBy(3);
    EXPECT_EQ(dropdown.topIndex(), 3);
    EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader + 1.0f), 3);
    EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader + 79.0f), 10);
    EXPECT_EQ(dropdown.itemTopOffsetY(kHeader, 3), kHeader);
    EXPECT_LT(dropdown.itemCenterOffsetY(kHeader, 2), static_cast<float>(kHeader));
}

TEST(WidgetDropdownWindow, RejectsTheHeaderAndRowsPastTheWindow) {
    IRComponents::C_WidgetDropdown dropdown = makeDropdown(20);
    EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader - 1.0f), -1);
    EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader + 8 * 10 + 1.0f), -1)
        << "the ninth row is not painted, so it names no item";
}

TEST(WidgetDropdownWindow, ClampsAndRevealsTheSelectedItem) {
    IRComponents::C_WidgetDropdown dropdown = makeDropdown(20);
    dropdown.scrollBy(100);
    EXPECT_EQ(dropdown.topIndex(), 12);
    dropdown.scrollBy(-100);
    EXPECT_EQ(dropdown.topIndex(), 0);

    dropdown.scrollIntoView(15);
    EXPECT_EQ(dropdown.topIndex(), 8) << "item 15 lands on the last visible row";
    EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader + 7 * 10 + 1.0f), 15);

    dropdown.scrollIntoView(9);
    EXPECT_EQ(dropdown.topIndex(), 8) << "a visible item moves nothing";
    dropdown.scrollIntoView(2);
    EXPECT_EQ(dropdown.topIndex(), 2) << "an item above lands on the top row";
    dropdown.scrollIntoView(-1);
    dropdown.scrollIntoView(99);
    EXPECT_EQ(dropdown.topIndex(), 2) << "an index outside the items is ignored";
}

TEST(WidgetDropdownWindow, LeavesShortListsAtTheTop) {
    for (int count : {8, 3}) {
        IRComponents::C_WidgetDropdown dropdown = makeDropdown(count);
        dropdown.scrollBy(3);
        EXPECT_EQ(dropdown.topIndex(), 0);
        EXPECT_EQ(dropdown.stripHeight(), count * dropdown.rowHeight());
        EXPECT_EQ(dropdown.itemAtOffsetY(kHeader, kHeader + 1.0f), 0);
        EXPECT_EQ(dropdown.itemTopOffsetY(kHeader, count - 1), kHeader + (count - 1) * 10);
    }
}

} // namespace
