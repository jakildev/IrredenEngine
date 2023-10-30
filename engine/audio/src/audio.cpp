/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\audio.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */


#include <irreden/audio/audio.hpp>

namespace IRAudio {

    Audio::Audio()
    :   m_rtAudio()
    ,   m_numDevices(m_rtAudio.getDeviceCount())
    {
        IRProfile::engLogInfo("Number of devices found: {}", m_numDevices);
        std::vector<unsigned int> deviceIds = m_rtAudio.getDeviceIds();
        for(auto& id : deviceIds) {
            m_deviceInfo.insert({id, m_rtAudio.getDeviceInfo(id)});
        }
        logDeviceInfoAll();
    }

    void Audio::openStreamIn(
        std::string deviceName,
        RtAudioCallback callback
    )
    {
        int deviceIndex = getDeviceIndexByName(deviceName);
        if(deviceIndex == -1) {
            IRProfile::engLogError("Device not found");
            return;
        }
        RtAudio::StreamParameters parameters;
        parameters.deviceId = deviceIndex;
        parameters.nChannels = 2;
        parameters.firstChannel = 0;

        unsigned int sampleRate = 44100;
        unsigned int bufferFrames = 256; // 256 sample frames

        m_rtAudio.openStream(
            nullptr,
            &parameters,
            RTAUDIO_SINT32,
            sampleRate,
            &bufferFrames,
            callback
        );
    }

    void Audio::logDeviceInfoAll() {
        for(auto& [id, info] : m_deviceInfo) {
            IRProfile::engLogInfo("Device: {}", info.name);
            if(info.outputChannels > 0) {
                IRProfile::engLogInfo("Output channels: {}", info.outputChannels);
            }
            if(info.inputChannels > 0) {
                IRProfile::engLogInfo("Input channels: {}", info.inputChannels);
            }
            if(info.duplexChannels > 0) {
                IRProfile::engLogInfo("Duplex channels: {}", info.duplexChannels);
            }
            if(info.isDefaultInput) {
                IRProfile::engLogInfo("This is default input device");
            }
            if(info.isDefaultOutput) {
                IRProfile::engLogInfo("This is default output device");
            }
            if(info.nativeFormats > 0) {
                IRProfile::engLogInfo("Native formats: {}", info.nativeFormats);
            }
            if(info.sampleRates.size() > 0) {
                IRProfile::engLogInfo("Sample rates: ");
                std::stringstream sampleRates{};
                for(auto& sampleRate : info.sampleRates) {
                    sampleRates << sampleRate << ", ";
                }
                IRProfile::engLogInfo("{}", sampleRates.str());
            }
            if(info.preferredSampleRate > 0) {
                IRProfile::engLogInfo("Preferred sample rate: {}", info.preferredSampleRate);
            }
        }
    }

    int Audio::getDeviceIndexByName(std::string deviceName) {
        for(auto& [id, info] : m_deviceInfo) {
            if(info.name == deviceName) {
                return id;
            }
        }
        return -1;
    }
}