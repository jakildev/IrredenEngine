#ifndef IR_VIDEO_H
#define IR_VIDEO_H

#include <irreden/video/ir_video_types.hpp>
#include <irreden/video/video_recorder.hpp>

namespace IRVideo {

extern VideoManager *g_videoManager;
VideoManager &getVideoManager();

bool startRecording(const VideoRecorderConfig &config);
void stopRecording();
bool recordFrame(const std::uint8_t *rgbaData, int strideBytes);
void toggleRecording();
bool isRecording();
std::uint64_t getFrameCount();
const std::string &getLastError();

} // namespace IRVideo

#endif /* IR_VIDEO_H */
