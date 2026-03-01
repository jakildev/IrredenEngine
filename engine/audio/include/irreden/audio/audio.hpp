#ifndef AUDIO_H
#define AUDIO_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio_capture_source.hpp>

#include <RtAudio.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace IRAudio {

class Audio : public IAudioCaptureSource {
  public:
    using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

    Audio();
    ~Audio() override;

    bool openStreamIn(
        const std::string &deviceName,
        int sampleRate,
        int channels,
        AudioInputCallback callback
    );
    bool startStreamIn();
    void stopStreamIn();
    void closeStreamIn();
    [[nodiscard]] bool isStreamInOpen() const;
    [[nodiscard]] bool isStreamInRunning() const;

    bool startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) override;
    void stopCapture() override;
    [[nodiscard]] bool isCapturing() const override;
    [[nodiscard]] double getInputLatencyMs() const override;

  private:
    mutable RtAudio m_rtAudio;
    std::unordered_map<unsigned int, RtAudio::DeviceInfo> m_deviceInfo;
    int m_numDevices = 0;
    bool m_streamInOpen = false;
    bool m_streamInRunning = false;
    int m_streamSampleRate = 48'000;
    AudioInputCallback m_inputCallback;

    void logDeviceInfoAll();
    int getDeviceIndexByName(const std::string &deviceName) const;
    unsigned int getDefaultInputDeviceId() const;
};

} // namespace IRAudio

#endif /* AUDIO_H */
