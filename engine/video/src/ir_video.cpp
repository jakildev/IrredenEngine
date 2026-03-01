#include <irreden/ir_video.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/video/video_manager.hpp>

namespace IRVideo {

VideoManager *g_videoManager = nullptr;
VideoManager &getVideoManager() {
    IR_ASSERT(g_videoManager != nullptr, "VideoManager not initialized");
    return *g_videoManager;
}

bool startRecording(const VideoRecorderConfig &config) {
    return getVideoManager().startRecording(config);
}

void stopRecording() {
    getVideoManager().stopRecording();
}

bool recordFrame(const std::uint8_t *rgbaData, int strideBytes) {
    return getVideoManager().recordFrame(rgbaData, strideBytes);
}

void configureScreenshotOutputDir(const std::string &outputDirPath) {
    getVideoManager().configureScreenshotOutputDir(outputDirPath);
}

void requestScreenshot() {
    getVideoManager().requestScreenshot();
}

void toggleRecording() {
    getVideoManager().toggleRecording();
}

void notifyFixedUpdate() {
    getVideoManager().notifyFixedUpdate();
}

bool isRecording() {
    return getVideoManager().isRecording();
}

std::uint64_t getFrameCount() {
    return getVideoManager().getFrameCount();
}

const std::string &getLastError() {
    return getVideoManager().getLastError();
}

} // namespace IRVideo
