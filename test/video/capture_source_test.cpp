#include <gtest/gtest.h>

#include <irreden/video/ir_video_types.hpp>

// The image a recording reads: `captureSource` picks the presented image (a
// screen post pass's output) by default and the raw composite only when the
// creation opted out of post effects or nothing was presented in its stead.
// Evaluated at compile time so the choice provably reads no engine state.

namespace {

using IRVideo::captureSource;

constexpr int kComposite = 1;
constexpr int kPresented = 2;

static_assert(captureSource(true, &kPresented, &kComposite) == &kPresented);
static_assert(captureSource(true, static_cast<const int *>(nullptr), &kComposite) == &kComposite);
static_assert(captureSource(false, &kPresented, &kComposite) == &kComposite);
static_assert(captureSource(false, static_cast<const int *>(nullptr), &kComposite) == &kComposite);

TEST(CaptureSourceTest, PresentedImageWinsOnlyWhenPostEffectsAreOnAndOneWasDrawn) {
    EXPECT_EQ(captureSource(true, &kPresented, &kComposite), &kPresented);
    EXPECT_EQ(captureSource(true, static_cast<const int *>(nullptr), &kComposite), &kComposite)
        << "an off or absent post pass leaves the composite as the clip";
    EXPECT_EQ(captureSource(false, &kPresented, &kComposite), &kComposite)
        << "the opt-out records the composite even while the screen is glitched";
}

} // namespace
