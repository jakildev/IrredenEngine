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

/// Resolved encoder output size, in pixels.
struct CaptureOutputResolution {
    int width_ = 0;
    int height_ = 0;
};

/// Derives the encoder output size from the two `configureCaptureOutputResolution`
/// overrides (0 = "derive from the render output") and the render output's
/// width/height. Both zero follows the render output; exactly one non-zero
/// derives the other from the render aspect, rounded down to even for
/// yuv420p; both non-zero keeps the caller's dimensions as given. Pure on
/// purpose — no GPU or render-manager access needed to test the derivation.
constexpr CaptureOutputResolution deriveCaptureOutputResolution(
    int widthOverride, int heightOverride, int renderWidth, int renderHeight
) {
    if (widthOverride <= 0 && heightOverride <= 0) {
        return CaptureOutputResolution{renderWidth, renderHeight};
    }
    if (heightOverride <= 0) {
        const int derivedHeight = (widthOverride * renderHeight / renderWidth) & ~1;
        return CaptureOutputResolution{widthOverride, derivedHeight};
    }
    if (widthOverride <= 0) {
        const int derivedWidth = (heightOverride * renderWidth / renderHeight) & ~1;
        return CaptureOutputResolution{derivedWidth, heightOverride};
    }
    return CaptureOutputResolution{widthOverride, heightOverride};
}

} // namespace IRVideo

#endif /* IR_VIDEO_TYPES_H */
