#ifndef IR_VIDEO_VIDEO_RECORDER_H
#define IR_VIDEO_VIDEO_RECORDER_H

#include <cstdint>
#include <string>

namespace IRVideo {

struct VideoRecorderConfig {
    std::string output_file_path_ = "capture.mp4";
    int width_ = 0;
    int height_ = 0;
    int target_fps_ = 60;
    int video_bitrate_ = 8'000'000;
};

class VideoRecorder {
  public:
    VideoRecorder();
    ~VideoRecorder();

    bool start(const VideoRecorderConfig &config);
    void stop();

    bool submitVideoFrame(const std::uint8_t *rgbaData, int strideBytes);

    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] std::uint64_t getVideoFrameCount() const;
    [[nodiscard]] const std::string &getLastError() const;

  private:
    bool m_isRecording = false;
    std::uint64_t m_videoFrameCount = 0;
    std::string m_lastError;

    struct FFmpegState;
    FFmpegState *m_state = nullptr;
};

} // namespace IRVideo

#endif /* IR_VIDEO_VIDEO_RECORDER_H */
