#ifndef IR_VIDEO_TYPES_H
#define IR_VIDEO_TYPES_H

namespace IRVideo {

class VideoManager;

/// Byte width of a single row in an RGBA frame buffer passed to @c recordFrame /
/// @c VideoRecorder::submitVideoFrame. Usually @c width * 4 for tightly-packed RGBA8.
using VideoStrideBytes = int;

/// Default capture frame rate used when no @c target_fps_ is specified in
/// @c VideoRecorderConfig.
constexpr int kDefaultCaptureFps = 60;

/// Lifecycle of the capture recorder as seen from the main thread.
/// @c FINALIZING is the window between a stop and the async finalize thread
/// closing the encoder, during which @c VideoManager::toggleCapture drops
/// every toggle — a caller that restarts a capture waits for @c IDLE first.
enum class RecordingState { IDLE, RECORDING, FINALIZING };

/// Derives the lifecycle state from the two atomic flags @c VideoManager
/// publishes. @c FINALIZING wins when both are set: a stop raises the
/// finalize flag before it clears the capture flag, and the recorder is
/// already shutting down in between. Pure on purpose — a @c constexpr body
/// cannot take the recorder mutex, which the finalize thread holds for the
/// whole encoder flush.
constexpr RecordingState recordingStateFrom(bool finalizeInProgress, bool captureEnabled) {
    if (finalizeInProgress) {
        return RecordingState::FINALIZING;
    }
    return captureEnabled ? RecordingState::RECORDING : RecordingState::IDLE;
}

} // namespace IRVideo

#endif /* IR_VIDEO_TYPES_H */
