#include <gtest/gtest.h>
#include <irreden/render/comparison_image.hpp>
#include <limits>

namespace {
using namespace IRRender;
ComparisonImage image(IRMath::ivec2 size, int red = 40) {
    return {
        size,
        std::vector<IRMath::Color>(
            static_cast<std::size_t>(size.x) * size.y,
            IRMath::Color{static_cast<std::uint8_t>(red), 0, 0, 255}
        )
    };
}
TEST(ComparisonImage, ReadbackOriginHasBackendSpecificOriginButCommonTopDownRows) {
    EXPECT_EQ(
        comparisonReadbackOrigin(
            {10, 20},
            {30, 40},
            {200, 100},
            IRPlatform::GraphicsBackend::OPENGL
        ),
        IRMath::ivec2(10, 40)
    );
    EXPECT_EQ(
        comparisonReadbackOrigin(
            {10, 20},
            {30, 40},
            {200, 100},
            IRPlatform::GraphicsBackend::METAL
        ),
        IRMath::ivec2(10, 20)
    );
}
TEST(ComparisonImage, NearestPresentationReplicatesPixelsAndReportsNativeClipping) {
    auto source = image({2, 1});
    source.pixels_[1].red_ = 80;
    const auto scaled = presentComparisonImage(source, {4, 2}, 2);
    ASSERT_TRUE(scaled.image_.valid());
    EXPECT_FALSE(scaled.clipped_);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(scaled.image_.pixels_[y * 4 + x].red_, x < 2 ? 40 : 80);
    const auto clipped = presentComparisonImage(source, {1, 1}, 1);
    EXPECT_TRUE(clipped.clipped_);
    EXPECT_EQ(clipped.image_.pixels_[0].red_, 80);
}
TEST(ComparisonImage, SharedRationalScalePreservesDifferentSilhouetteExtents) {
    auto fine = image({8, 4}, 0);
    auto coarse = image({4, 2}, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 2; x < 6; ++x)
            fine.pixels_[y * 8 + x].red_ = 255;
    for (int y = 0; y < 2; ++y)
        coarse.pixels_[y * 4 + 2].red_ = 255;
    const auto a = presentComparisonImage(fine, {4, 2}, 1, {}, 2);
    const auto b = presentComparisonImage(coarse, {4, 2}, 2, {}, 2);
    ASSERT_TRUE(a.image_.valid());
    ASSERT_TRUE(b.image_.valid());
    int fineCount = 0, coarseCount = 0;
    for (const auto c : a.image_.pixels_)
        fineCount += c.red_ == 255;
    for (const auto c : b.image_.pixels_)
        coarseCount += c.red_ == 255;
    EXPECT_EQ(fineCount, 4);
    EXPECT_EQ(coarseCount, 2);
    EXPECT_FALSE(a.clipped_);
    EXPECT_FALSE(b.clipped_);
}
TEST(ComparisonImage, InvalidImagesAndUnboundedScalesAreRejected) {
    auto source = image({2, 2});
    EXPECT_FALSE(presentComparisonImage(source, {2, 2}, 0).image_.valid());
    EXPECT_FALSE(presentComparisonImage(source, {2, 2}, 65).image_.valid());
    EXPECT_FALSE(presentComparisonImage(source, {2, 2}, 1, {}, 0).image_.valid());
    EXPECT_FALSE(presentComparisonImage(source, {2, 2}, 1, {}, 64).image_.valid());
    EXPECT_FALSE(presentComparisonImage(source, {1025, 1024}, 1).image_.valid());
    source.pixels_.pop_back();
    EXPECT_FALSE(presentComparisonImage(source, {2, 2}, 1).image_.valid());
}
TEST(ComparisonImage, OnionKeepsPositionsAndUsesExplicitCyanMagentaEndpoints) {
    auto a = image({2, 1}, 0), b = image({2, 1}, 0);
    a.pixels_[0] = {255, 255, 255, 255};
    b.pixels_[1] = {255, 255, 255, 255};
    const auto ref = onionComparisonImages(a, b, 0);
    EXPECT_EQ(ref.pixels_[0].red_, 0);
    EXPECT_EQ(ref.pixels_[0].green_, 255);
    EXPECT_EQ(ref.pixels_[0].blue_, 255);
    EXPECT_EQ(ref.pixels_[1].green_, 0);
    const auto selected = onionComparisonImages(a, b, 1);
    EXPECT_EQ(selected.pixels_[1].red_, 255);
    EXPECT_EQ(selected.pixels_[1].green_, 0);
    EXPECT_EQ(selected.pixels_[1].blue_, 255);
    EXPECT_EQ(selected.pixels_[0].red_, 0);
    EXPECT_FALSE(onionComparisonImages(a, image({1, 1}), .5f).valid());
    EXPECT_FALSE(onionComparisonImages(a, b, std::numeric_limits<float>::quiet_NaN()).valid());
}
} // namespace
