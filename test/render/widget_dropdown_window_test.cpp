#include <gtest/gtest.h>

#include <irreden/render/components/component_widget.hpp>

#include <string>

namespace {

using IRComponents::C_WidgetDropdown;
using StripLayout = IRComponents::C_WidgetDropdown::StripLayout;

C_WidgetDropdown makeDropdown(int count) {
    C_WidgetDropdown dropdown;
    dropdown.itemHeight_ = 10;
    for (int i = 0; i < count; ++i) {
        dropdown.items_.push_back("item " + std::to_string(i));
    }
    return dropdown;
}

constexpr int kHeader = 20;
// A header at y 0 on this canvas has room for far more than eight rows below.
constexpr int kTallCanvas = 400;

StripLayout belowLayout(const C_WidgetDropdown &dropdown) {
    return dropdown.stripLayout(0, kHeader, kTallCanvas);
}

TEST(WidgetDropdownWindow, LimitsTheStripToEightRowsAndMapsAbsoluteItems) {
    C_WidgetDropdown dropdown = makeDropdown(20);
    const StripLayout layout = belowLayout(dropdown);
    EXPECT_EQ(layout.offsetY_, kHeader);
    EXPECT_EQ(layout.rows_, IRComponents::kDropdownMaxVisibleRows);
    EXPECT_EQ(dropdown.stripHeight(layout), 8 * dropdown.rowHeight());
    EXPECT_EQ(dropdown.expandedOffsetY(layout), 0);
    EXPECT_EQ(dropdown.expandedHeight(kHeader, layout), kHeader + 80);

    dropdown.scrollBy(3, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 3);
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader + 1.0f), 3);
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader + 79.0f), 10);
    EXPECT_EQ(dropdown.itemTopOffsetY(layout, 3), kHeader);
    EXPECT_LT(dropdown.itemCenterOffsetY(layout, 2), static_cast<float>(kHeader));
}

TEST(WidgetDropdownWindow, RejectsTheHeaderAndRowsPastTheWindow) {
    C_WidgetDropdown dropdown = makeDropdown(20);
    const StripLayout layout = belowLayout(dropdown);
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader - 1.0f), -1);
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader + 8 * 10 + 1.0f), -1)
        << "the ninth row is not painted, so it names no item";
}

TEST(WidgetDropdownWindow, ClampsAndRevealsTheSelectedItem) {
    C_WidgetDropdown dropdown = makeDropdown(20);
    const StripLayout layout = belowLayout(dropdown);
    dropdown.scrollBy(100, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 12);
    dropdown.scrollBy(-100, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 0);

    dropdown.scrollIntoView(15, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 8) << "item 15 lands on the last visible row";
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader + 7 * 10 + 1.0f), 15);

    dropdown.scrollIntoView(9, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 8) << "a visible item moves nothing";
    dropdown.scrollIntoView(2, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 2) << "an item above lands on the top row";
    dropdown.scrollIntoView(-1, layout);
    dropdown.scrollIntoView(99, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 2) << "an index outside the items is ignored";
}

TEST(WidgetDropdownWindow, LeavesShortListsAtTheTop) {
    for (int count : {8, 3}) {
        C_WidgetDropdown dropdown = makeDropdown(count);
        const StripLayout layout = belowLayout(dropdown);
        dropdown.scrollBy(3, layout);
        EXPECT_EQ(dropdown.topIndex(layout), 0);
        EXPECT_EQ(dropdown.stripHeight(layout), count * dropdown.rowHeight());
        EXPECT_EQ(dropdown.itemAtOffsetY(layout, kHeader + 1.0f), 0);
        EXPECT_EQ(dropdown.itemTopOffsetY(layout, count - 1), kHeader + (count - 1) * 10);
    }
}

TEST(WidgetDropdownWindow, ZeroVisibleRowsKeepsTheEightRowDefaultWhereBothSidesHaveRoom) {
    C_WidgetDropdown dropdown = makeDropdown(40);
    ASSERT_EQ(dropdown.visibleRows_, 0);
    // 200 trixels above and 180 below: room for 20 and 18 rows.
    const StripLayout layout = dropdown.stripLayout(200, kHeader, kTallCanvas);
    EXPECT_EQ(layout.rows_, 8);
    EXPECT_EQ(layout.offsetY_, kHeader) << "below wins when the rows fit there";
}

TEST(WidgetDropdownWindow, FortyItemsNearTheCanvasBottomOpenUpwardInsideTheCanvas) {
    C_WidgetDropdown dropdown = makeDropdown(40);
    // Header y 365..385 on a 400-trixel canvas: one row fits below, 36 above.
    constexpr int kHeaderY = 365;
    const StripLayout layout = dropdown.stripLayout(kHeaderY, kHeader, kTallCanvas);
    ASSERT_EQ(layout.rows_, 8) << "the whole requested window fits above";
    EXPECT_EQ(layout.offsetY_, -8 * 10);
    const int stripTop = kHeaderY + layout.offsetY_;
    EXPECT_GE(stripTop, 0);
    EXPECT_EQ(stripTop + dropdown.stripHeight(layout), kHeaderY) << "the strip abuts the header";
    EXPECT_EQ(dropdown.expandedOffsetY(layout), -80);
    EXPECT_EQ(dropdown.expandedHeight(kHeader, layout), kHeader + 80);

    EXPECT_EQ(dropdown.itemAtOffsetY(layout, -80.0f + 1.0f), 0) << "top row";
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, -1.0f), 7) << "bottom row, just above the header";
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, 1.0f), -1) << "the header names no item";
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, -81.0f), -1) << "above the strip";

    dropdown.scrollBy(1, layout);
    EXPECT_EQ(dropdown.topIndex(layout), 1) << "a wheel step moves the upward window one row";
    EXPECT_EQ(dropdown.itemAtOffsetY(layout, -79.0f), 1);
    EXPECT_EQ(dropdown.itemTopOffsetY(layout, 1), -80);
}

TEST(WidgetDropdownWindow, NeitherSideFitsShrinksToTheRoomierSide) {
    C_WidgetDropdown dropdown = makeDropdown(40);
    dropdown.visibleRows_ = 0;
    // Canvas 100: header y 55..75. Room below 25 (2 rows), above 55 (5 rows).
    const StripLayout up = dropdown.stripLayout(55, kHeader, 100);
    EXPECT_EQ(up.rows_, 5) << "visibleRows_ = 0 asks for 8; 5 whole rows fit above";
    EXPECT_EQ(up.offsetY_, -50);
    EXPECT_GE(55 + up.offsetY_, 0);
    EXPECT_EQ(dropdown.itemAtOffsetY(up, static_cast<float>(up.offsetY_) + 1.0f), 0);
    EXPECT_EQ(dropdown.itemAtOffsetY(up, -1.0f), 4) << "the last painted row";
    dropdown.scrollBy(1, up);
    EXPECT_EQ(dropdown.topIndex(up), 1);
    EXPECT_EQ(dropdown.itemAtOffsetY(up, static_cast<float>(up.offsetY_) + 1.0f), 1);
    EXPECT_EQ(dropdown.itemAtOffsetY(up, -1.0f), 5);
    EXPECT_EQ(dropdown.maxScrollOffset(up), 35);

    // Header y 25..45: room below 55 (5 rows), above 25 (2 rows).
    const StripLayout down = dropdown.stripLayout(25, kHeader, 100);
    EXPECT_EQ(down.rows_, 5);
    EXPECT_EQ(down.offsetY_, kHeader);
    EXPECT_LE(25 + down.offsetY_ + dropdown.stripHeight(down), 100);

    // Header y 40..60: 40 trixels each side, 4 rows each: below wins the tie.
    const StripLayout tie = dropdown.stripLayout(40, kHeader, 100);
    EXPECT_EQ(tie.rows_, 4);
    EXPECT_EQ(tie.offsetY_, kHeader);
}

TEST(WidgetDropdownWindow, AuthoredVisibleRowsCapTheRequest) {
    C_WidgetDropdown dropdown = makeDropdown(40);
    dropdown.visibleRows_ = 3;
    EXPECT_EQ(dropdown.requestedRowCount(), 3);
    EXPECT_EQ(belowLayout(dropdown).rows_, 3);
    dropdown.visibleRows_ = 12;
    EXPECT_EQ(belowLayout(dropdown).rows_, 12) << "a cap above eight is honoured";
    dropdown.visibleRows_ = 99;
    EXPECT_EQ(belowLayout(dropdown).rows_, 38) << "the canvas room, not the cap, binds here";
    EXPECT_EQ(makeDropdown(2).requestedRowCount(), 2) << "never more rows than items";
}

TEST(WidgetDropdownWindow, NoCanvasOrNoItemsGivesAnEmptyStrip) {
    C_WidgetDropdown dropdown = makeDropdown(40);
    const StripLayout none = dropdown.stripLayout(100, kHeader, 0);
    EXPECT_EQ(none.rows_, 0);
    EXPECT_EQ(dropdown.itemAtOffsetY(none, kHeader + 1.0f), -1);
    dropdown.scrollIntoView(20, none);
    EXPECT_EQ(dropdown.scrollOffset_, 0) << "a window with no rows reveals nothing";

    const C_WidgetDropdown empty = makeDropdown(0);
    EXPECT_EQ(empty.stripLayout(0, kHeader, kTallCanvas).rows_, 0);
}

} // namespace
