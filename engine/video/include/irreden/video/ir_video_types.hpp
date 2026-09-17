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

} // namespace IRVideo

#endif /* IR_VIDEO_TYPES_H */
