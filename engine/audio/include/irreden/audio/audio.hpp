#ifndef AUDIO_H
#define AUDIO_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio_capture_source.hpp>

#include <RtAudio.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace IRAudio {

namespace detail {

// The RtAudio operations input capture drives, and nothing wider. Every
// status-returning call reports RTAUDIO_NO_ERROR as its only success value;
// getErrorText() is the diagnostic for the most recent non-success return.
class IAudioInputBackend {
  public:
    virtual ~IAudioInputBackend() = default;
    virtual std::vector<unsigned int> getDeviceIds() = 0;
    virtual RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) = 0;
    // Opens a float32 input-only stream. `bufferFrames` is in/out: the backend
    // may replace the requested size with the one it granted.
    virtual RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) = 0;
    virtual RtAudioErrorType startStream() = 0;
    virtual RtAudioErrorType stopStream() = 0;
    virtual void closeStream() = 0;
    virtual const std::string &getErrorText() = 0;
    virtual unsigned int getStreamSampleRate() = 0;
    virtual long getStreamLatency() = 0;
};

} // namespace detail

class Audio : public IAudioCaptureSource {
  public:
    using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

    Audio();
    explicit Audio(std::unique_ptr<detail::IAudioInputBackend> backend);
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
    [[nodiscard]] int getCaptureSampleRate() const override;
    [[nodiscard]] double getInputLatencyMs() const override;

  private:
    std::unique_ptr<detail::IAudioInputBackend> m_backend;
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
