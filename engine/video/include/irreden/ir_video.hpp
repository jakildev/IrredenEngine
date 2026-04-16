#ifndef IR_VIDEO_H
#define IR_VIDEO_H

#include <irreden/video/ir_video_types.hpp>
#include <irreden/video/video_recorder.hpp>
#include <string>

namespace IRVideo {

/// @cond INTERNAL
extern VideoManager *g_videoManager;
VideoManager &getVideoManager();
/// @endcond

/// @{
/// @name Recording control
/// Start a new recording session with the supplied @p config. Creates or overwrites
/// the output MP4. Returns @c false on FFmpeg initialisation failure; call
/// @c getLastError() for details.
bool startRecording(const VideoRecorderConfig &config);
/// Flush the encoder pipeline and close the output file. Safe to call when not recording.
void stopRecording();
/// Convenience toggle: calls @c startRecording / @c stopRecording based on current state.
/// Requires a previously configured @c VideoRecorderConfig (stored by @c VideoManager).
void toggleRecording();
/// @}

/// Submit a raw RGBA8 frame to the encoder. Must be called once per render frame while
/// recording is active. @p strideBytes is typically @c width * 4 for packed RGBA.
/// Returns @c false on encode failure.
bool recordFrame(const std::uint8_t *rgbaData, int strideBytes);

/// @{
/// @name Screenshot
/// Set the directory where PNG screenshots are written. Defaults to the working directory.
void configureScreenshotOutputDir(const std::string &outputDirPath);
/// Queue a full-screen (composite) screenshot to be written on the next render frame.
void requestScreenshot();
/// Queue a screenshot of the voxel canvas only — no UI or overlay layers.
void requestCanvasScreenshot();
/// @}

/// Notify the video manager that a fixed-update tick has elapsed. Call once per
/// fixed-step update so the encoder can align audio/video timestamps correctly.
void notifyFixedUpdate();

/// @{
/// @name Query
bool isRecording();
/// Total number of video frames submitted in the current recording session.
std::uint64_t getFrameCount();
/// Last error string set by FFmpeg.
const std::string &getLastError();
/// @}

} // namespace IRVideo

#endif /* IR_VIDEO_H */
