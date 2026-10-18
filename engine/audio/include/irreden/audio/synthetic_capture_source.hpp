#ifndef SYNTHETIC_CAPTURE_SOURCE_H
#define SYNTHETIC_CAPTURE_SOURCE_H

#include <irreden/audio/audio_capture_source.hpp>
#include <irreden/audio/audio_input_meter.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace IRAudio {

class SyntheticAudioCaptureSource : public IAudioCaptureSource {
  public:
    ~SyntheticAudioCaptureSource() override;

    bool startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) override;
    void stopCapture() override;
    [[nodiscard]] bool isCapturing() const override;
    [[nodiscard]] int getCaptureSampleRate() const override;
    [[nodiscard]] int getCaptureChannels() const override;
    [[nodiscard]] double getInputLatencyMs() const override;
    // The tone has no device to switch, so a take changes nothing.
    void setCaptureRecording(bool recording) override;
    [[nodiscard]] std::optional<AudioInputLevels> getInputLevels() const override;

  private:
    void captureLoop(AudioCaptureConfig config, AudioCaptureCallback callback);

    std::atomic<bool> m_capturing = false;
    std::atomic<int> m_captureSampleRate = 0;
    std::atomic<int> m_captureChannels = 0;
    detail::AudioInputMeter m_meter;
    std::condition_variable m_wakeCondition;
    std::mutex m_wakeMutex;
    std::thread m_captureThread;
};

} // namespace IRAudio

#endif /* SYNTHETIC_CAPTURE_SOURCE_H */
