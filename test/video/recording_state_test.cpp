#include <gtest/gtest.h>

#include <irreden/ir_video.hpp>
#include <irreden/video/ir_video_types.hpp>
#include <irreden/video/video_manager.hpp>

// Coverage for the recorder lifecycle query: `recordingStateFrom` (the pure
// flag → state derivation) and `IRVideo::recordingState()` (the `ir_video.hpp`
// surface over `VideoManager`).
//
// Only IDLE is reachable on a live `VideoManager` here. RECORDING and
// FINALIZING are entered by `toggleCapture`, which reads the `mainFramebuffer`
// entity and the render manager's output resolution — a GPU context this
// binary does not have. Those two states, and the precedence between them,
// are pinned on the derivation instead, evaluated at compile time: a body that
// constant-evaluates provably takes no lock, which is the whole contract of
// the query (the finalize thread holds the recorder mutex for the entire
// encoder flush, so a mutex-taking poll from the main thread would stall).

namespace {

using IRVideo::RecordingState;
using IRVideo::recordingStateFrom;

static_assert(recordingStateFrom(false, false) == RecordingState::IDLE);
static_assert(recordingStateFrom(false, true) == RecordingState::RECORDING);
static_assert(recordingStateFrom(true, false) == RecordingState::FINALIZING);
// `beginAsyncFinalize` raises the finalize flag before `toggleCapture` clears
// the capture flag; a poll landing between the two must not read RECORDING.
static_assert(recordingStateFrom(true, true) == RecordingState::FINALIZING);

TEST(RecordingStateTest, DerivationCoversEveryFlagCombination) {
    EXPECT_EQ(recordingStateFrom(false, false), RecordingState::IDLE);
    EXPECT_EQ(recordingStateFrom(false, true), RecordingState::RECORDING);
    EXPECT_EQ(recordingStateFrom(true, false), RecordingState::FINALIZING);
    EXPECT_EQ(recordingStateFrom(true, true), RecordingState::FINALIZING);
}

TEST(RecordingStateTest, FreshManagerReportsIdleThroughBothSurfaces) {
    IRVideo::VideoManager manager;

    EXPECT_EQ(manager.recordingState(), RecordingState::IDLE);
    EXPECT_FALSE(manager.isRecording());

    // The free function resolves through `g_videoManager`, which the
    // constructor registered.
    EXPECT_EQ(&IRVideo::getVideoManager(), &manager);
    EXPECT_EQ(IRVideo::recordingState(), RecordingState::IDLE);
    EXPECT_FALSE(IRVideo::isRecording());
}

TEST(RecordingStateTest, StopOnIdleManagerStaysIdle) {
    // A stop with nothing recording is the documented safe no-op; it must
    // neither flip the capture flag nor leave a finalize in flight.
    IRVideo::VideoManager manager;
    manager.stopRecording();

    EXPECT_EQ(manager.recordingState(), RecordingState::IDLE);
    EXPECT_FALSE(manager.isRecording());
}

} // namespace
