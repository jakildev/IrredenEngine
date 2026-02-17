#include <irreden/audio/audio.hpp>

namespace IRAudio {

Audio::Audio() : m_rtAudio(), m_numDevices(m_rtAudio.getDeviceCount()) {
    IRE_LOG_INFO("Number of devices found: {}", m_numDevices);
    std::vector<unsigned int> deviceIds = m_rtAudio.getDeviceIds();
    for (auto &id : deviceIds) {
        m_deviceInfo.insert({id, m_rtAudio.getDeviceInfo(id)});
    }
    logDeviceInfoAll();
}

void Audio::openStreamIn(std::string deviceName, RtAudioCallback callback) {
    int deviceIndex = getDeviceIndexByName(deviceName);
    if (deviceIndex == -1) {
        IRE_LOG_ERROR("Device not found");
        return;
    }
    RtAudio::StreamParameters parameters;
    parameters.deviceId = deviceIndex;
    parameters.nChannels = 2;
    parameters.firstChannel = 0;

    unsigned int sampleRate = 44100;
    unsigned int bufferFrames = 256; // 256 sample frames

    m_rtAudio.openStream(nullptr, &parameters, RTAUDIO_SINT32, sampleRate, &bufferFrames, callback);
}

void Audio::logDeviceInfoAll() {
    for (auto &[id, info] : m_deviceInfo) {
        IRE_LOG_INFO("Device: {}", info.name);
        if (info.outputChannels > 0) {
            IRE_LOG_INFO("Output channels: {}", info.outputChannels);
        }
        if (info.inputChannels > 0) {
            IRE_LOG_INFO("Input channels: {}", info.inputChannels);
        }
        if (info.duplexChannels > 0) {
            IRE_LOG_INFO("Duplex channels: {}", info.duplexChannels);
        }
        if (info.isDefaultInput) {
            IRE_LOG_INFO("This is default input device");
        }
        if (info.isDefaultOutput) {
            IRE_LOG_INFO("This is default output device");
        }
        if (info.nativeFormats > 0) {
            IRE_LOG_INFO("Native formats: {}", info.nativeFormats);
        }
        if (info.sampleRates.size() > 0) {
            IRE_LOG_INFO("Sample rates: ");
            std::stringstream sampleRates{};
            for (auto &sampleRate : info.sampleRates) {
                sampleRates << sampleRate << ", ";
            }
            IRE_LOG_INFO("{}", sampleRates.str());
        }
        if (info.preferredSampleRate > 0) {
            IRE_LOG_INFO("Preferred sample rate: {}", info.preferredSampleRate);
        }
    }
}

int Audio::getDeviceIndexByName(std::string deviceName) {
    for (auto &[id, info] : m_deviceInfo) {
        if (info.name == deviceName) {
            return id;
        }
    }
    return -1;
}
} // namespace IRAudio