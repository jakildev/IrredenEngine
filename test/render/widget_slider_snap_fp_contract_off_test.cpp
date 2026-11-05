#include "widget_slider_snap_test_cases.hpp"

TEST(WidgetSliderSnapFpContractOffTest, NonBinaryLatticesHaveCanonicalBits) {
    IRTest::detail::expectCanonicalSteppedFloatLattices();
}
