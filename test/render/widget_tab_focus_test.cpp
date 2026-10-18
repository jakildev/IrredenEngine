#include <gtest/gtest.h>

#include <irreden/render/widgets.hpp>

namespace {

TEST(WidgetTabFocusTest, CandidateExcludesTextInputsAndDisabledWidgets) {
    const IRComponents::C_Widget textInput{
        IRComponents::WidgetKind::TEXT_INPUT,
        IRMath::ivec2(10, 10)
    };
    IRComponents::C_Widget disabledButton{IRComponents::WidgetKind::BUTTON, IRMath::ivec2(10, 10)};
    disabledButton.disabled_ = true;

    EXPECT_FALSE(IRPrefab::Widget::isTabFocusCandidate(textInput));
    EXPECT_FALSE(IRPrefab::Widget::isTabFocusCandidate(disabledButton));
}

TEST(WidgetTabFocusTest, CandidateIncludesEnabledInteractiveWidgets) {
    EXPECT_TRUE(
        IRPrefab::Widget::isTabFocusCandidate(
            IRComponents::C_Widget{IRComponents::WidgetKind::BUTTON, IRMath::ivec2(10, 10)}
        )
    );
    EXPECT_TRUE(
        IRPrefab::Widget::isTabFocusCandidate(
            IRComponents::C_Widget{IRComponents::WidgetKind::SLIDER, IRMath::ivec2(10, 10)}
        )
    );
    EXPECT_TRUE(
        IRPrefab::Widget::isTabFocusCandidate(
            IRComponents::C_Widget{IRComponents::WidgetKind::LIST, IRMath::ivec2(10, 10)}
        )
    );
}

TEST(WidgetTabFocusTest, NumberFieldTakesPointerKeyboardFocusOnly) {
    const IRComponents::C_Widget numberField{
        IRComponents::WidgetKind::NUMBER_FIELD,
        IRMath::ivec2(80, 18)
    };
    EXPECT_FALSE(IRPrefab::Widget::isTabFocusCandidate(numberField));
    EXPECT_TRUE(IRPrefab::Widget::takesKeyboardFocus(numberField));

    IRComponents::C_Widget disabled = numberField;
    disabled.disabled_ = true;
    EXPECT_FALSE(IRPrefab::Widget::takesKeyboardFocus(disabled));
    EXPECT_FALSE(
        IRPrefab::Widget::takesKeyboardFocus(
            IRComponents::C_Widget{IRComponents::WidgetKind::BUTTON, IRMath::ivec2(80, 18)}
        )
    );
    EXPECT_FALSE(
        IRPrefab::Widget::takesKeyboardFocus(
            IRComponents::C_Widget{IRComponents::WidgetKind::SLIDER, IRMath::ivec2(80, 18)}
        )
    );
    EXPECT_TRUE(
        IRPrefab::Widget::takesKeyboardFocus(
            IRComponents::C_Widget{IRComponents::WidgetKind::TEXT_INPUT, IRMath::ivec2(80, 18)}
        )
    );
}

} // namespace
