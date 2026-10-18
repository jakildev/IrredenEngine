#ifndef IR_AUDIO_CAPTURE_SOURCE_H
#define IR_AUDIO_CAPTURE_SOURCE_H

#include <irreden/audio/ir_audio_types.hpp>

#include <functional>
#include <optional>
#include <string>

namespace IRAudio {

struct AudioCaptureConfig {
    std::string device_name_;
    int sample_rate_ = 48'000;
    int channels_ = 2;
    bool monitor_enabled_ = false;
    std::string monitor_device_name_;
};

using AudioCaptureCallback = std::function<void(const float *, int, double, bool)>;

class IAudioCaptureSource {
  public:
    virtual ~IAudioCaptureSource() = default;
    virtual bool startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) = 0;
    virtual void stopCapture() = 0;
    [[nodiscard]] virtual bool isCapturing() const = 0;
    /// Rate of delivered samples while capture is active, or 0 otherwise.
    [[nodiscard]] virtual int getCaptureSampleRate() const = 0;
    /// Interleaved channel count of delivered samples while capture is active,
    /// or 0 otherwise. Can differ from the requested count when the capture
    /// shares an already-open input.
    [[nodiscard]] virtual int getCaptureChannels() const = 0;
    [[nodiscard]] virtual double getInputLatencyMs() const = 0;
    /// Whether a take is consuming the delivered samples. A source that can
    /// switch its input device at runtime refuses to while this is set.
    virtual void setCaptureRecording(bool recording) = 0;
    /// The input's latest level-meter window, or nullopt when no input is
    /// open or the source does not meter. Safe from inside the callback.
    [[nodiscard]] virtual std::optional<AudioInputLevels> getInputLevels() const = 0;
};

} // namespace IRAudio

#endif /* IR_AUDIO_CAPTURE_SOURCE_H */
