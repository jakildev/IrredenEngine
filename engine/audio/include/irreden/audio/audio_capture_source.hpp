#ifndef IR_AUDIO_CAPTURE_SOURCE_H
#define IR_AUDIO_CAPTURE_SOURCE_H

#include <functional>
#include <string>

namespace IRAudio {

struct AudioCaptureConfig {
    std::string device_name_;
    int sample_rate_ = 48'000;
    int channels_ = 2;
};

using AudioCaptureCallback = std::function<void(const float *, int, double, bool)>;

class IAudioCaptureSource {
  public:
    virtual ~IAudioCaptureSource() = default;
    virtual bool startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) = 0;
    virtual void stopCapture() = 0;
    [[nodiscard]] virtual bool isCapturing() const = 0;
    [[nodiscard]] virtual double getInputLatencyMs() const = 0;
};

} // namespace IRAudio

#endif /* IR_AUDIO_CAPTURE_SOURCE_H */
