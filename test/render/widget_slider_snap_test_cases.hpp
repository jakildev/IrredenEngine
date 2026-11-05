#ifndef WIDGET_SLIDER_SNAP_TEST_CASES_H
#define WIDGET_SLIDER_SNAP_TEST_CASES_H

#include <gtest/gtest.h>

#include <irreden/render/widgets.hpp>

#include <bit>
#include <cstdint>

namespace IRTest::detail {

static void expectCanonicalSteppedFloatLattices() {
    struct TestCase {
        float min_;
        float max_;
        float step_;
        float value_;
        uint32_t expectedBits_;
    };
    constexpr TestCase cases[]{
        {0.4f, 0.9f, 0.025f, 0.7f, 0x3f333333u},
        {-3.1416f, 3.1416f, 0.39266f, 0.785f, 0x3f48f5c3u},
    };

    for (const TestCase &testCase : cases) {
        const float snapped = IRPrefab::Widget::snapSliderValue(
            testCase.min_,
            testCase.max_,
            testCase.step_,
            testCase.value_
        );
        EXPECT_EQ(std::bit_cast<uint32_t>(snapped), testCase.expectedBits_);
        EXPECT_EQ(
            IRPrefab::Widget::snapSliderValue(
                testCase.min_,
                testCase.max_,
                testCase.step_,
                snapped
            ),
            snapped
        );
    }
}

} // namespace IRTest::detail

#endif
