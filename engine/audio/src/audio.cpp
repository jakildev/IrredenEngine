#include <irreden/audio/audio.hpp>

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace {

class RtAudioInputBackend final : public IRAudio::detail::IAudioInputBackend {
  public:
    std::vector<unsigned int> getDeviceIds() override {
        return m_rtAudio.getDeviceIds();
    }

    RtAudio::DeviceInfo getDeviceInfo(unsigned int deviceId) override {
        return m_rtAudio.getDeviceInfo(deviceId);
    }

    RtAudioErrorType openInputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        return m_rtAudio.openStream(
            nullptr,
            &parameters,
            RTAUDIO_FLOAT32,
            sampleRate,
            &bufferFrames,
            std::move(callback),
            nullptr
        );
    }

    RtAudioErrorType startStream() override {
        return m_rtAudio.startStream();
    }

    RtAudioErrorType stopStream() override {
        return m_rtAudio.stopStream();
    }

    void closeStream() override {
        m_rtAudio.closeStream();
    }

    const std::string &getErrorText() override {
        return m_rtAudio.getErrorText();
    }

    long getStreamLatency() override {
        return m_rtAudio.getStreamLatency();
    }

  private:
    RtAudio m_rtAudio;
};

} // namespace

namespace IRAudio {

Audio::Audio()
    : Audio(std::make_unique<RtAudioInputBackend>()) {}

Audio::Audio(std::unique_ptr<detail::IAudioInputBackend> backend)
    : m_backend(std::move(backend)) {
    IR_ASSERT(m_backend != nullptr, "Audio requires an input backend");
    const std::vector<unsigned int> deviceIds = m_backend->getDeviceIds();
    m_numDevices = static_cast<int>(deviceIds.size());
    IRE_LOG_INFO("Number of devices found: {}", m_numDevices);
    for (const unsigned int id : deviceIds) {
        m_deviceInfo.insert({id, m_backend->getDeviceInfo(id)});
    }
    logDeviceInfoAll();
}

Audio::~Audio() {
    closeStreamIn();
}

bool Audio::openStreamIn(
    const std::string &deviceName,
    int sampleRate,
    int channels,
    AudioInputCallback callback
) {
    closeStreamIn();
    unsigned int deviceId = 0;
    if (!deviceName.empty()) {
        const int requestedDeviceId = getDeviceIndexByName(deviceName);
        if (requestedDeviceId < 0) {
            IRE_LOG_ERROR("Audio input device not found: {}", deviceName.c_str());
            return false;
        }
        deviceId = static_cast<unsigned int>(requestedDeviceId);
    } else {
        deviceId = getDefaultInputDeviceId();
        if (deviceId == 0) {
            IRE_LOG_ERROR("No default audio input device available.");
            return false;
        }
    }

    RtAudio::DeviceInfo deviceInfo = m_backend->getDeviceInfo(deviceId);
    const unsigned int requestedChannels =
        static_cast<unsigned int>(IRMath::clamp(channels, 1, 2));
    const unsigned int availableInputChannels = static_cast<unsigned int>(deviceInfo.inputChannels);
    if (availableInputChannels < requestedChannels) {
        IRE_LOG_ERROR(
            "Requested {} input channels but device '{}' provides {}.",
            requestedChannels,
            deviceInfo.name.c_str(),
            availableInputChannels
        );
        return false;
    }

    RtAudio::StreamParameters parameters;
    parameters.deviceId = deviceId;
    parameters.nChannels = requestedChannels;
    parameters.firstChannel = 0;

    const unsigned int requestedSampleRate =
        static_cast<unsigned int>(IRMath::max(sampleRate, 8'000));
    unsigned int bufferFrames = kAudioInputDefaultBufferFrames;

    // In place before the backend can deliver a buffer; a failed open clears
    // it again so a rejected callback is never retained.
    m_inputCallback = std::move(callback);
    RtAudioErrorType openResult = RTAUDIO_NO_ERROR;
    try {
        RtAudioCallback rtAudioCallback = [this](void *,
                                                 void *inputBuffer,
                                                 unsigned int nFrames,
                                                 double streamTime,
                                                 RtAudioStreamStatus status,
                                                 void *) -> int {
            if (m_inputCallback && inputBuffer != nullptr && nFrames > 0) {
                m_inputCallback(
                    static_cast<const float *>(inputBuffer),
                    static_cast<int>(nFrames),
                    streamTime,
                    (status & RTAUDIO_INPUT_OVERFLOW) != 0
                );
            }
            return 0;
        };
        openResult = m_backend->openInputStream(
            parameters,
            requestedSampleRate,
            bufferFrames,
            std::move(rtAudioCallback)
        );
    } catch (...) {
        m_inputCallback = {};
        IRE_LOG_ERROR("Failed to open audio input stream.");
        return false;
    }
    if (openResult != RTAUDIO_NO_ERROR) {
        m_inputCallback = {};
        IRE_LOG_ERROR(
            "Failed to open audio input stream: device='{}' sampleRate={} channels={}: {}",
            deviceInfo.name.c_str(),
            requestedSampleRate,
            requestedChannels,
            m_backend->getErrorText().c_str()
        );
        return false;
    }
    m_streamInOpen = true;
    m_streamSampleRate = static_cast<int>(requestedSampleRate);
    IRE_LOG_INFO(
        "Opened audio input stream: device='{}' sampleRate={} channels={} bufferFrames={}",
        deviceInfo.name.c_str(),
        requestedSampleRate,
        requestedChannels,
        bufferFrames
    );
    return true;
}

bool Audio::startStreamIn() {
    if (!m_streamInOpen) {
        IRE_LOG_ERROR("Cannot start audio input stream: stream is not open.");
        return false;
    }
    if (m_streamInRunning) {
        IRE_LOG_WARN("Audio input stream start requested, but stream is already running.");
        return true;
    }
    RtAudioErrorType startResult = RTAUDIO_NO_ERROR;
    try {
        startResult = m_backend->startStream();
    } catch (...) {
        IRE_LOG_ERROR("Failed to start audio input stream.");
        return false;
    }
    if (startResult != RTAUDIO_NO_ERROR) {
        IRE_LOG_ERROR("Failed to start audio input stream: {}", m_backend->getErrorText().c_str());
        return false;
    }
    m_streamInRunning = true;
    return true;
}

void Audio::stopStreamIn() {
    if (!m_streamInOpen) {
        IRE_LOG_WARN("Audio input stream stop requested, but stream is not open.");
        return;
    }
    if (!m_streamInRunning) {
        IRE_LOG_WARN("Audio input stream stop requested, but stream is not running.");
        return;
    }
    RtAudioErrorType stopResult = RTAUDIO_NO_ERROR;
    try {
        stopResult = m_backend->stopStream();
    } catch (...) {
        IRE_LOG_ERROR("Failed to stop audio input stream.");
        return;
    }
    if (stopResult != RTAUDIO_NO_ERROR) {
        IRE_LOG_ERROR("Failed to stop audio input stream: {}", m_backend->getErrorText().c_str());
        return;
    }
    m_streamInRunning = false;
}

void Audio::closeStreamIn() {
    if (!m_streamInOpen) {
        return;
    }
    if (m_streamInRunning) {
        stopStreamIn();
    }
    // closeStream() returns no status and tears down a stream that is still
    // running, so both flags clear even when the stop failed.
    try {
        m_backend->closeStream();
    } catch (...) {
        IRE_LOG_ERROR("Failed to close audio input stream.");
    }
    m_streamInOpen = false;
    m_streamInRunning = false;
    m_inputCallback = {};
}

bool Audio::isStreamInOpen() const {
    return m_streamInOpen;
}

bool Audio::isStreamInRunning() const {
    return m_streamInRunning;
}

bool Audio::startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) {
    if (!openStreamIn(config.device_name_, config.sample_rate_, config.channels_, std::move(cb))) {
        return false;
    }
    if (!startStreamIn()) {
        closeStreamIn();
        return false;
    }
    return true;
}

void Audio::stopCapture() {
    closeStreamIn();
}

bool Audio::isCapturing() const {
    return isStreamInRunning();
}

double Audio::getInputLatencyMs() const {
    if (!m_streamInOpen || m_streamSampleRate <= 0) {
        return 0.0;
    }
    const long latencyFrames = m_backend->getStreamLatency();
    return 1000.0 * static_cast<double>(latencyFrames) / static_cast<double>(m_streamSampleRate);
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

int Audio::getDeviceIndexByName(const std::string &deviceName) const {
    for (auto &[id, info] : m_deviceInfo) {
        if (info.name == deviceName) {
            return static_cast<int>(id);
        }
    }
    return -1;
}

unsigned int Audio::getDefaultInputDeviceId() const {
    for (const auto &[id, info] : m_deviceInfo) {
        if (info.isDefaultInput && info.inputChannels > 0) {
            return id;
        }
    }
    return 0;
}
} // namespace IRAudio