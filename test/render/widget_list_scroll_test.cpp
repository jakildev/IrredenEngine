#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/widgets.hpp>

#include <string>

// C_WidgetList's scroll geometry is the one description WIDGET_APPLY_LIST,
// WIDGET_RENDER_LIST and setListSelectedIndex share. The widget systems read
// the GUI canvas and cannot tick headless, so the geometry is pinned here.

namespace {

constexpr int kRowHeight = 13;
// 70 / 13 leaves five whole rows and a five-trixel partial row.
constexpr int kFiveRowView = 70;
constexpr int kThreeRowView = 44;

IRComponents::C_WidgetList makeListOf(int count) {
    IRComponents::C_WidgetList list;
    list.itemHeight_ = kRowHeight;
    for (int i = 0; i < count; ++i) {
        list.items_.push_back("item " + std::to_string(i));
    }
    return list;
}

TEST(WidgetListScroll, VisibleRowsCountsWholeRowsOnly) {
    const IRComponents::C_WidgetList list = makeListOf(7);
    EXPECT_EQ(list.visibleRows(kFiveRowView), 5);
    EXPECT_EQ(list.visibleRows(kThreeRowView), 3);
    EXPECT_EQ(list.visibleRows(kRowHeight - 1), 0);
}

TEST(WidgetListScroll, ZeroItemHeightDoesNotDivideByZero) {
    IRComponents::C_WidgetList list = makeListOf(3);
    list.itemHeight_ = 0;
    EXPECT_EQ(list.rowHeight(), 1);
    EXPECT_EQ(list.visibleRows(10), 10);
}

TEST(WidgetListScroll, TopIndexClampsAStaleOffsetAfterItemsShrink) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = 2;
    ASSERT_EQ(list.topIndex(kFiveRowView), 2);

    list.items_.resize(1);

    EXPECT_EQ(list.maxScrollOffset(kFiveRowView), 0);
    EXPECT_EQ(list.topIndex(kFiveRowView), 0);
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, 1.0f), 0);
}

TEST(WidgetListScroll, TopIndexClampsANegativeOffset) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = -3;
    EXPECT_EQ(list.topIndex(kFiveRowView), 0);
}

TEST(WidgetListScroll, ScrollByStopsAtBothEnds) {
    IRComponents::C_WidgetList list = makeListOf(7);

    list.scrollBy(1, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
    list.scrollBy(5, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 2);
    list.scrollBy(-1, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
    list.scrollBy(-9, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 0);
}

TEST(WidgetListScroll, ScrollByZeroWritesBackTheClampedOffset) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = 40;
    list.scrollBy(0, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 2);
}

TEST(WidgetListScroll, ScrollByNeverMovesAListThatFits) {
    IRComponents::C_WidgetList list = makeListOf(5);
    list.scrollBy(3, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 0);
}

TEST(WidgetListScroll, ScrollIntoViewPutsARowBelowTheViewOnTheBottomRow) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollIntoView(6, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 2);

    IRComponents::C_WidgetList narrow = makeListOf(13);
    narrow.scrollIntoView(8, kThreeRowView);
    EXPECT_EQ(narrow.scrollOffset_, 6);
}

TEST(WidgetListScroll, ScrollIntoViewPutsARowAboveTheViewOnTheTopRow) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = 2;
    list.scrollIntoView(1, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
}

TEST(WidgetListScroll, ScrollIntoViewLeavesAVisibleRowWhereItIs) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = 1;
    list.scrollIntoView(1, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
    list.scrollIntoView(5, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
    list.scrollIntoView(3, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
}

TEST(WidgetListScroll, ScrollIntoViewIgnoresAnIndexOutsideTheItems) {
    IRComponents::C_WidgetList list = makeListOf(7);
    list.scrollOffset_ = 1;
    list.scrollIntoView(-1, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
    list.scrollIntoView(7, kFiveRowView);
    EXPECT_EQ(list.scrollOffset_, 1);
}

TEST(WidgetListScroll, ItemAtOffsetYFollowsTheScrollOffset) {
    IRComponents::C_WidgetList list = makeListOf(7);
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, 0.0f), 0);
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, list.rowCenterOffsetY(4)), 4);

    list.scrollOffset_ = 2;
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, list.rowCenterOffsetY(0)), 2);
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, list.rowCenterOffsetY(4)), 6);
}

TEST(WidgetListScroll, ItemAtOffsetYRejectsPointsThatAreNotOnAnItemRow) {
    IRComponents::C_WidgetList list = makeListOf(7);
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, -0.5f), -1);
    // The trailing partial row, below the fifth whole row.
    EXPECT_EQ(list.itemAtOffsetY(kFiveRowView, 5.0f * kRowHeight + 1.0f), -1);

    const IRComponents::C_WidgetList shortList = makeListOf(2);
    EXPECT_EQ(shortList.itemAtOffsetY(kFiveRowView, shortList.rowCenterOffsetY(1)), 1);
    EXPECT_EQ(shortList.itemAtOffsetY(kFiveRowView, shortList.rowCenterOffsetY(2)), -1);
}

TEST(WidgetListScroll, ThumbSpanIsEmptyWithoutOverflow) {
    EXPECT_EQ(makeListOf(0).thumbSpan(kFiveRowView, 16).height_, 0);
    EXPECT_EQ(makeListOf(5).thumbSpan(kFiveRowView, 16).height_, 0);
}

TEST(WidgetListScroll, ThumbSpanStaysInsideTheViewAtEveryOffset) {
    IRComponents::C_WidgetList list = makeListOf(7);
    const int maxTop = list.maxScrollOffset(kFiveRowView);
    ASSERT_EQ(maxTop, 2);

    for (int top = 0; top <= maxTop; ++top) {
        list.scrollOffset_ = top;
        const IRComponents::C_WidgetList::ThumbSpan thumb = list.thumbSpan(kFiveRowView, 16);
        EXPECT_EQ(thumb.height_, kFiveRowView * 5 / 7);
        EXPECT_GE(thumb.offsetY_, 0);
        EXPECT_LE(thumb.offsetY_ + thumb.height_, kFiveRowView);
    }

    list.scrollOffset_ = 0;
    EXPECT_EQ(list.thumbSpan(kFiveRowView, 16).offsetY_, 0);
    list.scrollOffset_ = maxTop;
    const IRComponents::C_WidgetList::ThumbSpan atEnd = list.thumbSpan(kFiveRowView, 16);
    EXPECT_EQ(atEnd.offsetY_ + atEnd.height_, kFiveRowView);
}

TEST(WidgetListScroll, ThumbSpanHonoursTheMinimumHeight) {
    const IRComponents::C_WidgetList list = makeListOf(200);
    EXPECT_EQ(list.thumbSpan(kFiveRowView, 16).height_, 16);
    // A minimum taller than the view is capped at the view.
    EXPECT_EQ(list.thumbSpan(kFiveRowView, 500).height_, kFiveRowView);
}

class WidgetListScrollSelection : public testing::Test {
  protected:
    IREntity::EntityId makeSevenItemList() {
        return IRPrefab::Widget::makeList(
            IRMath::ivec2(0, 0),
            IRMath::ivec2(112, kFiveRowView),
            makeListOf(7).items_,
            0,
            kRowHeight
        );
    }

    static IRComponents::C_WidgetList &listOf(IREntity::EntityId widget) {
        return IREntity::getComponent<IRComponents::C_WidgetList>(widget);
    }

    IREntity::EntityManager m_entity_manager;
};

TEST_F(WidgetListScrollSelection, ChangingTheSelectionScrollsItIntoView) {
    const IREntity::EntityId widget = makeSevenItemList();

    IRPrefab::Widget::setListSelectedIndex(widget, 6);

    EXPECT_EQ(listOf(widget).selectedIndex_, 6);
    EXPECT_EQ(listOf(widget).scrollOffset_, 2);
}

// A caller that mirrors its state into the list every frame must not undo a
// wheel scroll that moved the selected row out of view.
TEST_F(WidgetListScrollSelection, ReselectingTheSameIndexLeavesTheScrollOffsetAlone) {
    const IREntity::EntityId widget = makeSevenItemList();
    IRPrefab::Widget::setListSelectedIndex(widget, 6);
    listOf(widget).scrollBy(-2, kFiveRowView);
    ASSERT_EQ(listOf(widget).scrollOffset_, 0);

    IRPrefab::Widget::setListSelectedIndex(widget, 6);

    EXPECT_EQ(listOf(widget).scrollOffset_, 0);
}

TEST_F(WidgetListScrollSelection, ClearingTheSelectionLeavesTheScrollOffsetAlone) {
    const IREntity::EntityId widget = makeSevenItemList();
    IRPrefab::Widget::setListSelectedIndex(widget, 6);

    IRPrefab::Widget::setListSelectedIndex(widget, -1);

    EXPECT_EQ(listOf(widget).selectedIndex_, -1);
    EXPECT_EQ(listOf(widget).scrollOffset_, 2);
}

} // namespace
