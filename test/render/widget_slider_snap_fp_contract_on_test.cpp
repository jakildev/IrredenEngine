#include "widget_slider_snap_test_cases.hpp"

TEST(WidgetSliderSnapFpContractOnTest, NonBinaryLatticesHaveCanonicalBits) {
    IRTest::detail::expectCanonicalSteppedFloatLattices();
}
