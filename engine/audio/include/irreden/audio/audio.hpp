#ifndef AUDIO_H
#define AUDIO_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/audio/audio_capture_source.hpp>

#include <RtAudio.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
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
    virtual RtAudioErrorType openOutputStream(
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

// How long a caller waits on the backend before abandoning the call. One arm
// attempt (device lookup, open, start) shares `arm_`; stop and close share
// `teardown_`. Only tests pass anything but the defaults.
struct AudioInputDeadlines {
    std::chrono::milliseconds arm_ = kAudioInputBackendDeadline;
    std::chrono::milliseconds teardown_ = kAudioInputBackendDeadline;
};

class AudioInputGate;
class AudioInputSession;
class AudioMonitorChannel;

} // namespace detail

// Main-thread only. Every backend call runs on one control thread that owns the
// backend, and the caller waits on it for at most a deadline: a call the OS
// never returns from costs that thread, not the caller. Past the deadline the
// operation reports failure, capture reads as closed, and no new backend call
// is accepted until the pending one returns and its stream has been closed.
class Audio : public IAudioCaptureSource {
  public:
    using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

    Audio();
    explicit Audio(
        std::unique_ptr<detail::IAudioInputBackend> backend,
        detail::AudioInputDeadlines deadlines = {}
    );
    Audio(
        std::unique_ptr<detail::IAudioInputBackend> inputBackend,
        std::unique_ptr<detail::IAudioInputBackend> outputBackend,
        detail::AudioInputDeadlines deadlines = {}
    );
    // Returns within the teardown deadline; a backend call still pending then
    // keeps the backend alive on the control thread.
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

    // Open and start share one arm deadline. A stream that opened but did not
    // start is closed on the control thread after this returns.
    bool startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) override;
    // No sample reaches the callback once this returns.
    void stopCapture() override;
    [[nodiscard]] bool isCapturing() const override;
    [[nodiscard]] int getCaptureSampleRate() const override;
    [[nodiscard]] double getInputLatencyMs() const override;
    void setInputMonitorEnabled(bool enabled);
    [[nodiscard]] bool isInputMonitorEnabled() const;

  private:
    // Shared with the control thread, which outlives this object while a
    // backend call is still pending.
    std::shared_ptr<detail::AudioInputSession> m_session;
    std::shared_ptr<detail::AudioInputSession> m_monitorSession;
    std::unique_ptr<detail::IAudioInputBackend> m_pendingMonitorBackend;
    // Shared with the callback the backend stores; non-null while a stream is open.
    std::shared_ptr<detail::AudioInputGate> m_gate;
    std::shared_ptr<detail::AudioMonitorChannel> m_monitorChannel;
    detail::AudioInputDeadlines m_deadlines;
    std::unordered_map<unsigned int, RtAudio::DeviceInfo> m_deviceInfo;
    int m_numDevices = 0;
    bool m_streamInOpen = false;
    bool m_streamInRunning = false;
    int m_streamSampleRate = 48'000;
    unsigned int m_streamDeviceId = 0;
    unsigned int m_streamChannels = 0;
    unsigned int m_streamBufferFrames = kAudioInputDefaultBufferFrames;
    std::string m_streamDeviceName;
    std::string m_monitorDeviceName;
    bool m_monitorStreamOpen = false;
    bool m_monitorStreamRunning = false;

    void enumerateDevices();
    bool armStreamIn(
        const std::string &deviceName,
        int sampleRate,
        int channels,
        AudioInputCallback callback,
        bool startAfterOpen,
        std::optional<std::chrono::steady_clock::time_point> deadline = std::nullopt
    );
    bool
    armMonitorBy(const AudioCaptureConfig &config, std::chrono::steady_clock::time_point deadline);
    void closeMonitorBy(std::chrono::steady_clock::time_point deadline);
    void unpublishMonitor();
    void closeStreamInBy(std::chrono::steady_clock::time_point deadline);
    void unpublishStreamIn();
    void logDeviceInfoAll();
    int getDeviceIndexByName(const std::string &deviceName) const;
    unsigned int getDefaultInputDeviceId() const;
    unsigned int getDefaultOutputDeviceId() const;
};

} // namespace IRAudio

#endif /* AUDIO_H */
