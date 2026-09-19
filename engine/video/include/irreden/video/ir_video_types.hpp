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

namespace detail {

/// Rounds down to even for yuv420p, floored at 2 so a degenerate override
/// (0 or 1) never hands the encoder a zero-sized dimension.
constexpr int floorCaptureDimension(int value) {
    const int even = value & ~1;
    return even < 2 ? 2 : even;
}

} // namespace detail

/// Resolves the encoder output size from the two `configureCaptureOutputResolution`
/// overrides (`<= 0` = unset) and the render output's width/height. Both
/// unset passes the render pair through unrounded, so that path stays
/// byte-identical to following the render output directly; any set
/// dimension, and every derived one, rounds down to even and floors at 2.
/// A render pair with a dimension `< 1` (the viewport is 0×0 before the
/// window reports its first size, and while minimized) has no aspect to
/// derive from and counts as square, so the derived axis mirrors the set
/// one instead of dividing by zero. Pure on purpose — no GPU or
/// render-manager access needed to test the derivation.
constexpr CaptureOutputResolution resolveCaptureOutputResolution(
    int overrideWidth, int overrideHeight, int renderWidth, int renderHeight
) {
    if (overrideWidth <= 0 && overrideHeight <= 0) {
        return CaptureOutputResolution{renderWidth, renderHeight};
    }
    const bool hasRenderAspect = renderWidth > 0 && renderHeight > 0;
    const int aspectWidth = hasRenderAspect ? renderWidth : 1;
    const int aspectHeight = hasRenderAspect ? renderHeight : 1;
    if (overrideHeight <= 0) {
        const int width = detail::floorCaptureDimension(overrideWidth);
        const int height = detail::floorCaptureDimension(width * aspectHeight / aspectWidth);
        return CaptureOutputResolution{width, height};
    }
    if (overrideWidth <= 0) {
        const int height = detail::floorCaptureDimension(overrideHeight);
        const int width = detail::floorCaptureDimension(height * aspectWidth / aspectHeight);
        return CaptureOutputResolution{width, height};
    }
    return CaptureOutputResolution{
        detail::floorCaptureDimension(overrideWidth),
        detail::floorCaptureDimension(overrideHeight)
    };
}

} // namespace IRVideo

#endif /* IR_VIDEO_TYPES_H */
