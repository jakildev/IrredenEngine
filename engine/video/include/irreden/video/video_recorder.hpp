#ifndef IR_VIDEO_VIDEO_RECORDER_H
#define IR_VIDEO_VIDEO_RECORDER_H

#include <cstdint>
#include <string>
#include <vector>

namespace IRVideo {

struct VideoRecorderConfig {
    std::string output_file_path_ = "capture.mp4";
    int width_ = 0;
    int height_ = 0;
    int source_width_ = 0;
    int source_height_ = 0;
    int target_fps_ = 60;
    int video_bitrate_ = 8'000'000;
    bool capture_audio_input_ = false;
    std::string audio_input_device_name_ = "";
    int audio_sample_rate_ = 48'000;
    int audio_channels_ = 2;
    int audio_bitrate_ = 320'000;
    bool audio_lossless_ = false;
    bool audio_mux_enabled_ = true;
    bool audio_wav_enabled_ = true;
    double audio_sync_offset_ms_ = 0.0;
};

class VideoRecorder {
  public:
    VideoRecorder();
    ~VideoRecorder();

    bool start(const VideoRecorderConfig &config);
    void stop();

    bool submitVideoFrame(const std::uint8_t *rgbaData, int strideBytes);
    bool submitVideoFrame(std::vector<std::uint8_t> &&frameData);
    bool submitAudioInputSamples(const float *interleavedSamples, int frameCount, double streamTime);
    std::vector<std::uint8_t> acquireFrameBuffer(std::size_t minCapacity);

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
