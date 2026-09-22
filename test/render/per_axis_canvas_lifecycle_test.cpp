#include <gtest/gtest.h>

#include <irreden/render/per_axis_canvas.hpp>

#include <stdexcept>

// The per-axis set's lifecycle policy with the ECS lookups and GPU calls lifted
// out (`IRPrefab::PerAxisCanvas::lifecycleStep`): a frame sequence that
// rotates, crosses a cardinal and rotates again runs headlessly here, and the
// parked window is pinned to the constant syncAllocationToCameraYaw reads.

namespace {

using IRPrefab::PerAxisCanvas::kParkedCardinalFrames;
using IRPrefab::PerAxisCanvas::LifecycleState;
using IRPrefab::PerAxisCanvas::LifecycleStep;
using IRPrefab::PerAxisCanvas::lifecycleStep;

TEST(PerAxisCanvasLifecycle, RotationAllocatesOnceAndThenKeeps) {
    EXPECT_EQ(lifecycleStep({.rotating_ = true}), LifecycleStep::ALLOCATE);
    EXPECT_EQ(lifecycleStep({.rotating_ = true, .live_ = true}), LifecycleStep::KEEP);
}

TEST(PerAxisCanvasLifecycle, CardinalFrameParksTheLiveSet) {
    EXPECT_EQ(lifecycleStep({.live_ = true}), LifecycleStep::PARK);
}

TEST(PerAxisCanvasLifecycle, RotationResumingOnAParkedSetUnparksWhateverItWaited) {
    for (int waited : {0, 1, kParkedCardinalFrames - 1}) {
        EXPECT_EQ(
            lifecycleStep(
                {.rotating_ = true, .parked_ = true, .parkedFits_ = true, .parkedFrames_ = waited}
            ),
            LifecycleStep::UNPARK
        ) << "waited "
          << waited;
    }
}

TEST(PerAxisCanvasLifecycle, ParkedSetSizedForAnotherCanvasIsReplaced) {
    EXPECT_EQ(
        lifecycleStep({.rotating_ = true, .parked_ = true, .parkedFits_ = false}),
        LifecycleStep::REPLACE_PARKED
    );
}

TEST(PerAxisCanvasLifecycle, ParkedSetIsFreedOnTheFrameItsWindowCloses) {
    EXPECT_EQ(
        lifecycleStep({.parked_ = true, .parkedFrames_ = kParkedCardinalFrames - 1}),
        LifecycleStep::KEEP
    );
    EXPECT_EQ(
        lifecycleStep({.parked_ = true, .parkedFrames_ = kParkedCardinalFrames}),
        LifecycleStep::RELEASE_PARKED
    );
}

TEST(PerAxisCanvasLifecycle, NothingResidentOnACardinalIsKept) {
    EXPECT_EQ(lifecycleStep({}), LifecycleStep::KEEP);
    EXPECT_EQ(lifecycleStep({.parkedFrames_ = kParkedCardinalFrames}), LifecycleStep::KEEP);
}

// The bookkeeping syncAllocationToCameraYaw does around the policy: the parked
// counter advances on a cardinal frame with nothing live, and each step moves
// the set the way the component's park / unpark / allocate / release do.
struct SetModel {
    bool live_ = false;
    bool parked_ = false;
    int parkedFrames_ = 0;

    LifecycleStep frame(bool rotating) {
        if (!rotating && !live_ && parked_) {
            ++parkedFrames_;
        }
        const LifecycleStep step = lifecycleStep(
            {.rotating_ = rotating,
             .live_ = live_,
             .parked_ = parked_,
             .parkedFits_ = true,
             .parkedFrames_ = parkedFrames_}
        );
        switch (step) {
        case LifecycleStep::KEEP:
            break;
        case LifecycleStep::ALLOCATE:
        case LifecycleStep::REPLACE_PARKED:
            live_ = true;
            parked_ = false;
            break;
        case LifecycleStep::UNPARK:
            live_ = true;
            parked_ = false;
            parkedFrames_ = 0;
            break;
        case LifecycleStep::PARK:
            live_ = false;
            parked_ = true;
            parkedFrames_ = 0;
            break;
        case LifecycleStep::RELEASE_PARKED:
            parked_ = false;
            parkedFrames_ = 0;
            break;
        }
        return step;
    }
};

// A sweep that steps across a cardinal one frame per pose: the crossing costs
// no allocation and no release, which is what the profile report's
// PerAxisCanvas rows witness on the pinned sweep.
TEST(PerAxisCanvasLifecycle, SweepAcrossACardinalNeitherAllocatesNorFreesAgain) {
    SetModel set;
    EXPECT_EQ(set.frame(true), LifecycleStep::ALLOCATE);
    EXPECT_EQ(set.frame(true), LifecycleStep::KEEP);
    EXPECT_EQ(set.frame(false), LifecycleStep::PARK);
    EXPECT_EQ(set.frame(true), LifecycleStep::UNPARK);
    EXPECT_EQ(set.frame(true), LifecycleStep::KEEP);
    EXPECT_EQ(set.frame(false), LifecycleStep::PARK);
    EXPECT_EQ(set.frame(true), LifecycleStep::UNPARK);
    EXPECT_TRUE(set.live_);
    EXPECT_FALSE(set.parked_);
}

// A camera that stops on a cardinal: the set waits out the window and is freed
// exactly once, on the kParkedCardinalFrames-th cardinal frame after the park.
TEST(PerAxisCanvasLifecycle, CameraSettlingOnACardinalFreesTheSetAfterTheWindow) {
    SetModel set;
    ASSERT_EQ(set.frame(true), LifecycleStep::ALLOCATE);
    ASSERT_EQ(set.frame(false), LifecycleStep::PARK);
    for (int frame = 1; frame < kParkedCardinalFrames; ++frame) {
        ASSERT_EQ(set.frame(false), LifecycleStep::KEEP) << "cardinal frame " << frame;
    }
    EXPECT_EQ(set.frame(false), LifecycleStep::RELEASE_PARKED);
    EXPECT_FALSE(set.parked_);
    EXPECT_EQ(set.frame(false), LifecycleStep::KEEP);
    EXPECT_EQ(set.frame(true), LifecycleStep::ALLOCATE);
}

// A pause on a cardinal one frame short of the window keeps the set; the
// resumed rotation unparks it and the counter starts over at the next park.
TEST(PerAxisCanvasLifecycle, PauseInsideTheWindowKeepsTheSetAndRestartsTheCount) {
    SetModel set;
    ASSERT_EQ(set.frame(true), LifecycleStep::ALLOCATE);
    ASSERT_EQ(set.frame(false), LifecycleStep::PARK);
    for (int frame = 1; frame < kParkedCardinalFrames; ++frame) {
        ASSERT_EQ(set.frame(false), LifecycleStep::KEEP);
    }
    EXPECT_EQ(set.frame(true), LifecycleStep::UNPARK);
    EXPECT_EQ(set.frame(false), LifecycleStep::PARK);
    EXPECT_EQ(set.parkedFrames_, 0);
    EXPECT_EQ(set.frame(false), LifecycleStep::KEEP);
    EXPECT_EQ(set.parkedFrames_, 1);
}

#ifndef IR_RELEASE
// The swaps refuse an impossible state: parking with nothing live, unparking
// with nothing parked. A default-constructed component holds neither set and
// needs no device, so both guards fire here.
TEST(PerAxisCanvasLifecycle, ParkAndUnparkRefuseAnEmptySet) {
    IRComponents::C_PerAxisTrixelCanvases axes;
    EXPECT_THROW(axes.park(), std::runtime_error);
    EXPECT_THROW(axes.unpark(), std::runtime_error);
    EXPECT_FALSE(axes.isAllocated());
    EXPECT_FALSE(axes.hasParked());
}
#endif

} // namespace
