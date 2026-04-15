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

} // namespace IRVideo

#endif /* IR_VIDEO_TYPES_H */
