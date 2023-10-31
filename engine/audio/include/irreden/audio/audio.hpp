/*
 * Project: Irreden Engine
 * File: audio.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef AUDIO_H
#define AUDIO_H

// #include <RtAudio/RtAudio.h>
#include <RtAudio.h>
#include <irreden/ir_profile.hpp>
#include <string>

#include <sstream>

namespace IRAudio {

    class Audio {
    public:
        Audio();

        void openStreamIn(
            std::string deviceName,
            RtAudioCallback callback
        );
    private:
        RtAudio m_rtAudio;
        std::unordered_map<
            unsigned int,
            RtAudio::DeviceInfo
        > m_deviceInfo;
        int m_numDevices;

        void logDeviceInfoAll();
        int getDeviceIndexByName(std::string deviceName);
    };

    int audioInCallback();

} // namespace IRAudio

#endif /* AUDIO_H */
