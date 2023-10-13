/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\audio\audio.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */


#include "audio.hpp"

namespace IRAudio {

    Audio::Audio()
    :   m_rtAudio()
    ,   m_numDevices(m_rtAudio.getDeviceCount())
    {
        for(int i = 0; i < m_numDevices; i++) {
            m_deviceInfo.push_back(m_rtAudio.getDeviceInfo(i));
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
            ENG_LOG_ERROR("Device not found");
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
        for(int i = 0; i < m_numDevices; i++) {
            RtAudio::DeviceInfo info = m_rtAudio.getDeviceInfo(i);
            ENG_LOG_INFO("Device: {}", info.name);
            if(info.probed == false) {
                ENG_LOG_INFO("Device not probed");
            }
            if(info.outputChannels > 0) {
                ENG_LOG_INFO("Output channels: {}", info.outputChannels);
            }
            if(info.inputChannels > 0) {
                ENG_LOG_INFO("Input channels: {}", info.inputChannels);
            }
            if(info.duplexChannels > 0) {
                ENG_LOG_INFO("Duplex channels: {}", info.duplexChannels);
            }
            if(info.isDefaultInput) {
                ENG_LOG_INFO("This is default input device");
            }
            if(info.isDefaultOutput) {
                ENG_LOG_INFO("This is default output device");
            }
            if(info.nativeFormats > 0) {
                ENG_LOG_INFO("Native formats: {}", info.nativeFormats);
            }
            if(info.sampleRates.size() > 0) {
                ENG_LOG_INFO("Sample rates: ");
                std::stringstream sampleRates{};
                for(auto& sampleRate : info.sampleRates) {
                    sampleRates << sampleRate << ", ";
                }
                ENG_LOG_INFO("{}", sampleRates.str());
            }
            if(info.preferredSampleRate > 0) {
                ENG_LOG_INFO("Preferred sample rate: {}", info.preferredSampleRate);
            }
        }
    }

    int Audio::getDeviceIndexByName(std::string deviceName) {
        for(int i = 0; i < m_numDevices; i++) {
            if(m_deviceInfo[i].name == deviceName) {
                return i;
            }
        }
        return -1;
    }

}