#ifndef AUDIO_H
#define AUDIO_H

#include <irreden/ir_profile.hpp>

#include <irreden/audio/ir_audio_types.hpp>

#include <RtAudio.h>

#include <functional>
#include <string>
#include <unordered_map>

namespace IRAudio {

class Audio {
  public:
    using AudioInputCallback = std::function<void(const float *, int, double, bool)>;

    Audio();
    ~Audio();

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

  private:
    RtAudio m_rtAudio;
    std::unordered_map<unsigned int, RtAudio::DeviceInfo> m_deviceInfo;
    int m_numDevices = 0;
    bool m_streamInOpen = false;
    bool m_streamInRunning = false;
    AudioInputCallback m_inputCallback;

    void logDeviceInfoAll();
    int getDeviceIndexByName(const std::string &deviceName) const;
    unsigned int getDefaultInputDeviceId() const;
};

} // namespace IRAudio

#endif /* AUDIO_H */
