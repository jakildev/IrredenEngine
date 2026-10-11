#include <irreden/audio/audio.hpp>
#include <irreden/audio/audio_monitor_ring.hpp>

#include <irreden/ir_math.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
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

    RtAudioErrorType openOutputStream(
        RtAudio::StreamParameters &parameters,
        unsigned int sampleRate,
        unsigned int &bufferFrames,
        RtAudioCallback callback
    ) override {
        return m_rtAudio.openStream(
            &parameters,
            nullptr,
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

    unsigned int getStreamSampleRate() override {
        return m_rtAudio.getStreamSampleRate();
    }

    long getStreamLatency() override {
        return m_rtAudio.getStreamLatency();
    }

  private:
    RtAudio m_rtAudio;
};

} // namespace

namespace IRAudio {

namespace detail {

class AudioMonitorChannel {
  public:
    void configure(
        std::size_t capacityFrames,
        unsigned int channels,
        std::size_t targetFillFrames,
        std::size_t highWaterFrames
    ) {
        m_ring = std::make_unique<AudioMonitorRing>(
            capacityFrames,
            channels,
            targetFillFrames,
            highWaterFrames
        );
        m_outputCallbacks.store(0, std::memory_order_relaxed);
    }

    void openAdmission() {
        m_requestedEnabled.store(true, std::memory_order_release);
        m_admitting.store(true, std::memory_order_release);
    }

    void push(const float *samples, std::size_t frameCount) {
        if (m_admitting.load(std::memory_order_acquire) &&
            m_requestedEnabled.load(std::memory_order_acquire) && m_ring != nullptr) {
            m_ring->push(samples, frameCount);
        }
    }

    void render(float *samples, std::size_t frameCount) {
        m_outputCallbacks.fetch_add(1, std::memory_order_relaxed);
        if (!m_admitting.load(std::memory_order_acquire) ||
            !m_requestedEnabled.load(std::memory_order_acquire) || m_ring == nullptr) {
            if (m_ring != nullptr) {
                m_ring->silence(samples, frameCount);
            }
            return;
        }
        m_ring->consume(samples, frameCount);
    }

    void setRequestedEnabled(bool enabled) {
        const bool wasEnabled = m_requestedEnabled.exchange(enabled, std::memory_order_acq_rel);
        if (m_ring != nullptr && wasEnabled != enabled) {
            m_ring->requestReset();
        }
    }

    void setOutputRunning(bool running) {
        m_outputRunning.store(running, std::memory_order_release);
    }

    void closeAdmission() {
        m_admitting.store(false, std::memory_order_release);
        m_requestedEnabled.store(false, std::memory_order_release);
        m_outputRunning.store(false, std::memory_order_release);
    }

    [[nodiscard]] bool effectiveEnabled() const {
        return m_admitting.load(std::memory_order_acquire) &&
               m_outputRunning.load(std::memory_order_acquire) &&
               m_requestedEnabled.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t targetFillFrames() const {
        return m_ring == nullptr ? 0 : m_ring->targetFillFrames();
    }

    [[nodiscard]] std::uint64_t copiedFrames() const {
        return m_ring == nullptr ? 0 : m_ring->copiedFrames();
    }

    [[nodiscard]] std::uint64_t underrunFrames() const {
        return m_ring == nullptr ? 0 : m_ring->underrunFrames();
    }

    [[nodiscard]] std::uint64_t overrunFrames() const {
        return m_ring == nullptr ? 0 : m_ring->overrunFrames();
    }

    [[nodiscard]] std::uint64_t discardedFrames() const {
        return m_ring == nullptr ? 0 : m_ring->discardedFrames();
    }

    [[nodiscard]] std::uint64_t outputCallbacks() const {
        return m_outputCallbacks.load(std::memory_order_relaxed);
    }

  private:
    std::unique_ptr<AudioMonitorRing> m_ring;
    std::atomic<bool> m_admitting = false;
    std::atomic<bool> m_requestedEnabled = false;
    std::atomic<bool> m_outputRunning = false;
    std::atomic<std::uint64_t> m_outputCallbacks = 0;
};

// Carries the engine's sample callback to the driver thread. The callback the
// backend stores holds the gate, never the session that owns the backend, so
// the two cannot keep each other alive.
class AudioInputGate {
  public:
    AudioInputGate(
        Audio::AudioInputCallback callback, std::shared_ptr<AudioMonitorChannel> monitorChannel
    )
        : m_callback(std::move(callback))
        , m_monitorChannel(std::move(monitorChannel)) {}

    // Driver thread; never blocks.
    void deliver(const float *samples, int frameCount, double streamTime, bool overflow) {
        m_inFlight.fetch_add(1);
        if (!m_closed.load()) {
            if (m_callback) {
                m_callback(samples, frameCount, streamTime, overflow);
            }
            m_monitorChannel->push(samples, static_cast<std::size_t>(frameCount));
        }
        m_inFlight.fetch_sub(1);
        m_inFlight.notify_all();
    }

    // Returns once no delivery is in flight; none starts afterwards. Must not
    // be called from inside a delivery.
    void close() {
        m_closed.store(true);
        for (int inFlight = m_inFlight.load(); inFlight != 0; inFlight = m_inFlight.load()) {
            m_inFlight.wait(inFlight);
        }
        m_callback = {};
    }

  private:
    // deliver() reads it only between its in-flight increment and its closed
    // check, which is what lets close() clear it without a lock.
    Audio::AudioInputCallback m_callback;
    std::shared_ptr<AudioMonitorChannel> m_monitorChannel;
    std::atomic<bool> m_closed = false;
    std::atomic<int> m_inFlight = 0;
};

// The control thread's view of the backend. No other thread touches it while
// a task or a quarantine cleanup is running.
struct AudioInputBackendStream {
    std::unique_ptr<IAudioInputBackend> backend_;
    bool open_ = false;
};

// Owns the backend and the one thread that calls into it. The thread holds a
// reference to the session, so a call that never returns keeps the backend
// alive without anything else waiting on it.
class AudioInputSession {
  public:
    using Clock = std::chrono::steady_clock;
    // `abandoned` turns true once the caller has stopped waiting. A task must
    // not log or reach any engine global: it can still be running after the
    // caller and every manager are gone.
    using Task =
        std::function<void(AudioInputBackendStream &stream, const std::atomic<bool> &abandoned)>;

    enum class Completion { DONE, TIMED_OUT, QUARANTINED };

    // A null `injected` builds the RtAudio backend on the control thread, so
    // the COM apartment RtAudio's constructor enters on Windows belongs to the
    // thread that makes every later call and runs its destructor.
    static std::shared_ptr<AudioInputSession> create(std::unique_ptr<IAudioInputBackend> injected) {
        auto session = std::make_shared<AudioInputSession>();
        session->m_thread = std::thread([session, backend = std::move(injected)]() mutable {
            session->controlLoop(std::move(backend));
        });
        return session;
    }

    // Runs `task` on the control thread and waits for it until `deadline`
    // (forever when unset). TIMED_OUT quarantines the session: no task is
    // accepted until the pending one has returned and the control thread has
    // closed whatever stream it left open.
    Completion run(std::optional<Clock::time_point> deadline, Task task) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (m_quarantined) {
            return Completion::QUARANTINED;
        }
        m_task = std::move(task);
        m_taskOutstanding = true;
        m_wake.notify_one();
        const auto finished = [this] { return !m_taskOutstanding; };
        if (!deadline) {
            m_finished.wait(lock, finished);
            return Completion::DONE;
        }
        if (m_finished.wait_until(lock, *deadline, finished)) {
            return Completion::DONE;
        }
        m_quarantined = true;
        return Completion::TIMED_OUT;
    }

    // Hands an open stream to the control thread to close, without waiting.
    void quarantine() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quarantined = true;
        }
        m_wake.notify_one();
    }

    [[nodiscard]] bool isQuarantined() const {
        return m_quarantined;
    }

    // Caller side, valid only while a stream is open: an open stream means no
    // task is running and the session is not quarantined, so the control
    // thread is idle.
    [[nodiscard]] long streamLatencyFrames() {
        return m_stream.backend_->getStreamLatency();
    }

    // Ends the control thread. Returns true when the backend was destroyed
    // before `deadline`; otherwise the control thread destroys it whenever its
    // pending call returns.
    bool release(Clock::time_point deadline) {
        const Completion completion =
            run(deadline, [](AudioInputBackendStream &stream, const std::atomic<bool> &) {
                closeOpenStream(stream);
                stream.backend_.reset();
            });
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
        }
        m_wake.notify_one();
        if (completion != Completion::DONE) {
            m_thread.detach();
            return false;
        }
        m_thread.join();
        return true;
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::condition_variable m_finished;
    Task m_task;
    bool m_taskOutstanding = false;
    // Written under m_mutex; a running task reads it without the lock.
    std::atomic<bool> m_quarantined = false;
    bool m_released = false;
    AudioInputBackendStream m_stream;
    std::thread m_thread;

    static void closeOpenStream(AudioInputBackendStream &stream) {
        if (!stream.open_) {
            return;
        }
        try {
            stream.backend_->closeStream();
        } catch (...) {
        }
        stream.open_ = false;
    }

    void controlLoop(std::unique_ptr<IAudioInputBackend> injected) {
        if (injected) {
            m_stream.backend_ = std::move(injected);
        } else {
            m_stream.backend_ = std::make_unique<RtAudioInputBackend>();
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            m_wake.wait(lock, [this] { return m_task || m_quarantined || m_released; });
            if (m_task) {
                Task task = std::exchange(m_task, nullptr);
                lock.unlock();
                task(m_stream, m_quarantined);
                task = nullptr;
                lock.lock();
                m_taskOutstanding = false;
                m_finished.notify_all();
            } else if (m_quarantined) {
                lock.unlock();
                closeOpenStream(m_stream);
                lock.lock();
                m_quarantined = false;
            } else {
                break;
            }
        }
        lock.unlock();
        m_stream.backend_.reset();
    }
};

} // namespace detail

namespace {

using Clock = detail::AudioInputSession::Clock;
using Completion = detail::AudioInputSession::Completion;

std::string formatSampleRates(const std::vector<unsigned int> &sampleRates) {
    std::stringstream formatted;
    formatted << '[';
    for (std::size_t i = 0; i < sampleRates.size(); ++i) {
        if (i > 0) {
            formatted << ", ";
        }
        formatted << sampleRates[i];
    }
    formatted << ']';
    return formatted.str();
}

unsigned int
selectSampleRate(unsigned int requestedSampleRate, const std::vector<unsigned int> &sampleRates) {
    if (sampleRates.empty() ||
        std::find(sampleRates.begin(), sampleRates.end(), requestedSampleRate) !=
            sampleRates.end()) {
        return requestedSampleRate;
    }

    unsigned int selectedSampleRate = 0;
    std::int64_t selectedDistance = 0;
    for (const unsigned int sampleRate : sampleRates) {
        if (sampleRate < 8'000) {
            continue;
        }
        const std::int64_t distance = IRMath::abs(
            static_cast<std::int64_t>(sampleRate) - static_cast<std::int64_t>(requestedSampleRate)
        );
        if (selectedSampleRate == 0 || distance < selectedDistance ||
            (distance == selectedDistance && sampleRate > selectedSampleRate)) {
            selectedSampleRate = sampleRate;
            selectedDistance = distance;
        }
    }
    return selectedSampleRate == 0 ? requestedSampleRate : selectedSampleRate;
}

enum class BackendOperation { DEVICE_LOOKUP, OPEN, START, STOP, CLOSE };

const char *operationName(BackendOperation operation) {
    switch (operation) {
    case BackendOperation::DEVICE_LOOKUP:
        return "device lookup";
    case BackendOperation::OPEN:
        return "open";
    case BackendOperation::START:
        return "start";
    case BackendOperation::STOP:
        return "stop";
    case BackendOperation::CLOSE:
        return "close";
    }
    return "call";
}

enum class CallOutcome { NOT_RUN, SUCCEEDED, FAILED, THREW };

// Reports are filled on the control thread and read by the caller only after
// the task that fills them has completed. `operation_` is the exception: the
// caller reads it at the deadline to name the call still in progress.
struct CallReport {
    CallOutcome outcome_ = CallOutcome::NOT_RUN;
    std::string errorText_;
};

struct ArmReport {
    std::atomic<BackendOperation> operation_ = BackendOperation::DEVICE_LOOKUP;
    std::string deviceName_;
    std::vector<unsigned int> listedSampleRates_;
    unsigned int attemptedSampleRate_ = 0;
    // 0 when the backend does not report the rate it opened at.
    unsigned int backendSampleRate_ = 0;
    unsigned int availableInputChannels_ = 0;
    unsigned int bufferFrames_ = kAudioInputDefaultBufferFrames;
    bool channelsAvailable_ = false;
    CallReport open_;
    CallReport start_;
};

struct MonitorArmReport {
    std::atomic<BackendOperation> operation_ = BackendOperation::DEVICE_LOOKUP;
    std::string deviceName_;
    unsigned int deviceId_ = 0;
    unsigned int availableOutputChannels_ = 0;
    unsigned int backendSampleRate_ = 0;
    unsigned int bufferFrames_ = 0;
    bool channelsAvailable_ = false;
    bool deviceFound_ = false;
    bool rateAvailable_ = false;
    bool backendRateMatched_ = false;
    CallReport open_;
    CallReport start_;
    bool cleanupThrew_ = false;
    std::chrono::steady_clock::duration elapsed_{};
};

struct CloseReport {
    std::atomic<BackendOperation> operation_ = BackendOperation::CLOSE;
    CallReport stop_;
    bool closeThrew_ = false;
};

template <typename Call> CallReport callBackend(detail::IAudioInputBackend &backend, Call &&call) {
    CallReport report;
    try {
        if (call() == RTAUDIO_NO_ERROR) {
            report.outcome_ = CallOutcome::SUCCEEDED;
        } else {
            report.outcome_ = CallOutcome::FAILED;
            report.errorText_ = backend.getErrorText();
        }
    } catch (...) {
        report.outcome_ = CallOutcome::THREW;
    }
    return report;
}

const char *callFailureText(const CallReport &report) {
    return report.outcome_ == CallOutcome::THREW ? "backend call threw" : report.errorText_.c_str();
}

// Runs one status-returning backend call on the control thread. Empty when the
// call had not returned `limit` after the request.
template <typename Call>
std::optional<CallReport>
runBackendCall(detail::AudioInputSession &session, std::chrono::milliseconds limit, Call call) {
    auto report = std::make_shared<CallReport>();
    const Completion completion = session.run(
        Clock::now() + limit,
        [report, call](detail::AudioInputBackendStream &stream, const std::atomic<bool> &) {
            detail::IAudioInputBackend &backend = *stream.backend_;
            *report = callBackend(backend, [&] { return call(backend); });
        }
    );
    if (completion == Completion::TIMED_OUT) {
        return std::nullopt;
    }
    return *report;
}

bool logIfCallFailed(const char *operation, const CallReport &report) {
    if (report.outcome_ == CallOutcome::SUCCEEDED) {
        return false;
    }
    if (report.outcome_ == CallOutcome::FAILED) {
        IRE_LOG_ERROR("Failed to {} audio input stream: {}", operation, report.errorText_);
    } else {
        IRE_LOG_ERROR("Failed to {} audio input stream.", operation);
    }
    return true;
}

void logBackendTimeout(
    const char *consequence,
    BackendOperation operation,
    std::chrono::milliseconds limit,
    const std::string &deviceName
) {
    IRE_LOG_ERROR(
        "{}: backend {} did not return within {} ms (device='{}'); check microphone permission "
        "for the host app. The pending call stays on the audio control thread.",
        consequence,
        operationName(operation),
        limit.count(),
        deviceName
    );
}

void logMonitorTimeout(
    BackendOperation operation, std::chrono::milliseconds limit, const std::string &deviceName
) {
    IRE_LOG_WARN(
        "Audio input monitoring unavailable: output backend {} did not return within {} ms "
        "(device='{}'); input capture remains active.",
        operationName(operation),
        limit.count(),
        deviceName
    );
}

constexpr const char *kArmTimedOut = "Audio input unavailable";
constexpr const char *kTeardownTimedOut = "Audio input teardown not confirmed";

} // namespace

Audio::Audio()
    : m_session(detail::AudioInputSession::create(nullptr))
    , m_monitorChannel(std::make_shared<detail::AudioMonitorChannel>()) {
    enumerateDevices();
}

Audio::Audio(
    std::unique_ptr<detail::IAudioInputBackend> backend, detail::AudioInputDeadlines deadlines
)
    : Audio(std::move(backend), nullptr, deadlines) {}

Audio::Audio(
    std::unique_ptr<detail::IAudioInputBackend> inputBackend,
    std::unique_ptr<detail::IAudioInputBackend> outputBackend,
    detail::AudioInputDeadlines deadlines
)
    : m_pendingMonitorBackend(std::move(outputBackend))
    , m_monitorChannel(std::make_shared<detail::AudioMonitorChannel>())
    , m_deadlines(deadlines) {
    IR_ASSERT(inputBackend != nullptr, "Audio requires an input backend");
    m_session = detail::AudioInputSession::create(std::move(inputBackend));
    enumerateDevices();
}

Audio::~Audio() {
    const Clock::time_point deadline = Clock::now() + m_deadlines.teardown_;
    if (m_monitorChannel) {
        m_monitorChannel->closeAdmission();
    }
    closeStreamInBy(deadline);
    closeMonitorBy(deadline);
    if (!m_session->release(deadline)) {
        IRE_LOG_WARN(
            "Audio input backend still has a call pending; the audio control thread destroys it "
            "when that call returns."
        );
    }
    if (m_monitorSession && !m_monitorSession->release(deadline)) {
        IRE_LOG_WARN(
            "Audio monitor backend still has a call pending; the audio control thread destroys "
            "it when that call returns."
        );
    }
}

void Audio::enumerateDevices() {
    auto devices = std::make_shared<std::unordered_map<unsigned int, RtAudio::DeviceInfo>>();
    m_session->run(
        std::nullopt,
        [devices](detail::AudioInputBackendStream &stream, const std::atomic<bool> &) {
            for (const unsigned int id : stream.backend_->getDeviceIds()) {
                devices->insert({id, stream.backend_->getDeviceInfo(id)});
            }
        }
    );
    m_deviceInfo = std::move(*devices);
    m_numDevices = static_cast<int>(m_deviceInfo.size());
    IRE_LOG_INFO("Number of devices found: {}", m_numDevices);
    logDeviceInfoAll();
}

bool Audio::openStreamIn(
    const std::string &deviceName, int sampleRate, int channels, AudioInputCallback callback
) {
    return armStreamIn(deviceName, sampleRate, channels, std::move(callback), false);
}

bool Audio::armStreamIn(
    const std::string &deviceName,
    int sampleRate,
    int channels,
    AudioInputCallback callback,
    bool startAfterOpen,
    std::optional<std::chrono::steady_clock::time_point> deadline
) {
    closeStreamIn();
    if (m_session->isQuarantined()) {
        IRE_LOG_ERROR(
            "Audio input unavailable: an earlier backend call has not returned; not starting "
            "another."
        );
        return false;
    }
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

    const unsigned int requestedChannels =
        static_cast<unsigned int>(IRMath::clamp(channels, 1, 2));
    const unsigned int requestedSampleRate =
        static_cast<unsigned int>(IRMath::max(sampleRate, 8'000));
    const std::string enumeratedDeviceName = m_deviceInfo.at(deviceId).name;

    auto gate = std::make_shared<detail::AudioInputGate>(std::move(callback), m_monitorChannel);
    auto report = std::make_shared<ArmReport>();
    const Completion completion = m_session->run(
        deadline.value_or(Clock::now() + m_deadlines.arm_),
        [report, gate, deviceId, requestedChannels, requestedSampleRate, startAfterOpen](
            detail::AudioInputBackendStream &stream,
            const std::atomic<bool> &abandoned
        ) {
            detail::IAudioInputBackend &backend = *stream.backend_;
            const RtAudio::DeviceInfo deviceInfo = backend.getDeviceInfo(deviceId);
            report->deviceName_ = deviceInfo.name;
            report->availableInputChannels_ = deviceInfo.inputChannels;
            report->channelsAvailable_ = deviceInfo.inputChannels >= requestedChannels;
            report->listedSampleRates_ = deviceInfo.sampleRates;
            report->attemptedSampleRate_ =
                selectSampleRate(requestedSampleRate, deviceInfo.sampleRates);
            if (!report->channelsAvailable_ || abandoned) {
                return;
            }

            report->operation_ = BackendOperation::OPEN;
            RtAudio::StreamParameters parameters;
            parameters.deviceId = deviceId;
            parameters.nChannels = requestedChannels;
            parameters.firstChannel = 0;
            RtAudioCallback rtAudioCallback = [gate](
                                                  void *,
                                                  void *inputBuffer,
                                                  unsigned int nFrames,
                                                  double streamTime,
                                                  RtAudioStreamStatus status,
                                                  void *
                                              ) -> int {
                if (inputBuffer != nullptr && nFrames > 0) {
                    gate->deliver(
                        static_cast<const float *>(inputBuffer),
                        static_cast<int>(nFrames),
                        streamTime,
                        (status & RTAUDIO_INPUT_OVERFLOW) != 0
                    );
                }
                return 0;
            };
            report->open_ = callBackend(backend, [&] {
                return backend.openInputStream(
                    parameters,
                    report->attemptedSampleRate_,
                    report->bufferFrames_,
                    std::move(rtAudioCallback)
                );
            });
            if (report->open_.outcome_ != CallOutcome::SUCCEEDED) {
                return;
            }
            stream.open_ = true;
            report->backendSampleRate_ = backend.getStreamSampleRate();
            if (!startAfterOpen || abandoned) {
                return;
            }

            report->operation_ = BackendOperation::START;
            report->start_ = callBackend(backend, [&] { return backend.startStream(); });
        }
    );

    if (completion == Completion::TIMED_OUT) {
        gate->close();
        logBackendTimeout(kArmTimedOut, report->operation_, m_deadlines.arm_, enumeratedDeviceName);
        return false;
    }
    if (!report->channelsAvailable_) {
        gate->close();
        IRE_LOG_ERROR(
            "Requested {} input channels but device '{}' provides {}.",
            requestedChannels,
            report->deviceName_.c_str(),
            report->availableInputChannels_
        );
        return false;
    }
    const unsigned int attemptedSampleRate = report->attemptedSampleRate_;
    if (attemptedSampleRate != requestedSampleRate) {
        IRE_LOG_WARN(
            "Audio input sample rate substituted: device='{}' requestedRate={} "
            "attemptedRate={} listedRates={}",
            report->deviceName_.c_str(),
            requestedSampleRate,
            attemptedSampleRate,
            formatSampleRates(report->listedSampleRates_)
        );
    }
    if (report->open_.outcome_ == CallOutcome::THREW) {
        gate->close();
        IRE_LOG_ERROR(
            "Failed to open audio input stream: device='{}' requestedRate={} "
            "attemptedRate={} channels={}.",
            report->deviceName_.c_str(),
            requestedSampleRate,
            attemptedSampleRate,
            requestedChannels
        );
        return false;
    }
    if (report->open_.outcome_ != CallOutcome::SUCCEEDED) {
        gate->close();
        IRE_LOG_ERROR(
            "Failed to open audio input stream: device='{}' requestedRate={} "
            "attemptedRate={} channels={}: {}",
            report->deviceName_.c_str(),
            requestedSampleRate,
            attemptedSampleRate,
            requestedChannels,
            report->open_.errorText_.c_str()
        );
        return false;
    }
    if (startAfterOpen && logIfCallFailed("start", report->start_)) {
        gate->close();
        m_session->quarantine();
        IRE_LOG_WARN(
            "Audio input stream opened but did not start; the audio control thread closes it "
            "without the caller waiting."
        );
        return false;
    }

    m_gate = std::move(gate);
    m_streamInOpen = true;
    m_streamInRunning = startAfterOpen;
    const unsigned int actualSampleRate =
        report->backendSampleRate_ == 0 ? attemptedSampleRate : report->backendSampleRate_;
    m_streamSampleRate = static_cast<int>(actualSampleRate);
    m_streamChannels = requestedChannels;
    m_streamBufferFrames = report->bufferFrames_;
    m_streamDeviceName = report->deviceName_;
    if (actualSampleRate != attemptedSampleRate) {
        IRE_LOG_WARN(
            "Audio input backend adjusted sample rate: device='{}' requestedRate={} "
            "attemptedRate={} actualRate={}",
            report->deviceName_.c_str(),
            requestedSampleRate,
            attemptedSampleRate,
            actualSampleRate
        );
    }
    IRE_LOG_INFO(
        "Opened audio input stream: device='{}' sampleRate={} channels={} bufferFrames={}",
        report->deviceName_.c_str(),
        actualSampleRate,
        requestedChannels,
        report->bufferFrames_
    );
    return true;
}

bool Audio::armMonitorBy(
    const AudioCaptureConfig &config, std::chrono::steady_clock::time_point deadline
) {
    if (!m_monitorSession) {
        m_monitorSession = detail::AudioInputSession::create(std::move(m_pendingMonitorBackend));
    }
    if (m_monitorSession->isQuarantined()) {
        IRE_LOG_WARN(
            "Audio input monitoring unavailable: an earlier output backend call has not "
            "returned; input capture remains active."
        );
        return false;
    }
    auto report = std::make_shared<MonitorArmReport>();
    auto channel = m_monitorChannel;
    const std::string requestedDeviceName = config.monitor_device_name_;
    const std::string inputDeviceName = m_streamDeviceName;
    const auto startedAt = Clock::now();
    const Completion completion = m_monitorSession->run(
        deadline,
        [report,
         channel,
         requestedDeviceName,
         inputDeviceName,
         channels = m_streamChannels,
         inputBufferFrames = m_streamBufferFrames,
         sampleRate = static_cast<unsigned int>(
             m_streamSampleRate
         )](detail::AudioInputBackendStream &stream, const std::atomic<bool> &abandoned) {
            detail::IAudioInputBackend &backend = *stream.backend_;
            RtAudio::DeviceInfo deviceInfo;
            for (const unsigned int id : backend.getDeviceIds()) {
                const RtAudio::DeviceInfo candidate = backend.getDeviceInfo(id);
                const bool matchesName =
                    !requestedDeviceName.empty() && candidate.name == requestedDeviceName;
                const bool matchesDefault = requestedDeviceName.empty() &&
                                            candidate.isDefaultOutput &&
                                            candidate.outputChannels > 0;
                if (matchesName || matchesDefault) {
                    report->deviceId_ = id;
                    deviceInfo = candidate;
                    report->deviceFound_ = true;
                    break;
                }
            }
            if (!report->deviceFound_ || abandoned) {
                return;
            }
            report->deviceName_ = deviceInfo.name;
            report->availableOutputChannels_ = deviceInfo.outputChannels;
            report->channelsAvailable_ = deviceInfo.outputChannels >= channels;
            report->rateAvailable_ =
                deviceInfo.sampleRates.empty() || std::find(
                                                      deviceInfo.sampleRates.begin(),
                                                      deviceInfo.sampleRates.end(),
                                                      sampleRate
                                                  ) != deviceInfo.sampleRates.end();
            if (!report->channelsAvailable_ || !report->rateAvailable_ || abandoned) {
                return;
            }

            report->bufferFrames_ = inputBufferFrames;
            if (deviceInfo.name != inputDeviceName) {
                report->bufferFrames_ = IRMath::min(
                    report->bufferFrames_,
                    static_cast<unsigned int>(IRMath::max(sampleRate / 100, 1U))
                );
            }

            report->operation_ = BackendOperation::OPEN;
            RtAudio::StreamParameters parameters;
            parameters.deviceId = report->deviceId_;
            parameters.nChannels = channels;
            parameters.firstChannel = 0;
            RtAudioCallback callback = [channel](
                                           void *outputBuffer,
                                           void *,
                                           unsigned int nFrames,
                                           double,
                                           RtAudioStreamStatus,
                                           void *
                                       ) -> int {
                channel->render(static_cast<float *>(outputBuffer), nFrames);
                return 0;
            };
            report->open_ = callBackend(backend, [&] {
                return backend.openOutputStream(
                    parameters,
                    sampleRate,
                    report->bufferFrames_,
                    std::move(callback)
                );
            });
            if (report->open_.outcome_ != CallOutcome::SUCCEEDED) {
                return;
            }
            stream.open_ = true;
            report->backendSampleRate_ = backend.getStreamSampleRate();
            report->backendRateMatched_ =
                report->backendSampleRate_ == 0 || report->backendSampleRate_ == sampleRate;
            if (!report->backendRateMatched_ || abandoned) {
                try {
                    backend.closeStream();
                    stream.open_ = false;
                } catch (...) {
                    report->cleanupThrew_ = true;
                }
                return;
            }

            const unsigned int targetFillFrames =
                IRMath::max(report->bufferFrames_, inputBufferFrames);
            const std::size_t capacityFrames = IRMath::nextPowerOfTwo(targetFillFrames * 8U);
            channel->configure(capacityFrames, channels, targetFillFrames, capacityFrames * 3 / 4);
            report->operation_ = BackendOperation::START;
            report->start_ = callBackend(backend, [&] { return backend.startStream(); });
            if (report->start_.outcome_ != CallOutcome::SUCCEEDED) {
                try {
                    backend.closeStream();
                    stream.open_ = false;
                } catch (...) {
                    report->cleanupThrew_ = true;
                }
            }
        }
    );
    report->elapsed_ = Clock::now() - startedAt;

    if (completion == Completion::TIMED_OUT) {
        m_monitorChannel->closeAdmission();
        logMonitorTimeout(
            report->operation_,
            m_deadlines.arm_,
            report->deviceName_.empty() ? requestedDeviceName : report->deviceName_
        );
        return false;
    }
    if (report->cleanupThrew_) {
        IRE_LOG_WARN(
            "Audio monitor output cleanup threw for device '{}'; the control thread retains "
            "the backend for teardown.",
            report->deviceName_
        );
    }
    if (!report->deviceFound_) {
        m_monitorChannel->closeAdmission();
        if (requestedDeviceName.empty()) {
            IRE_LOG_WARN(
                "Audio input monitoring unavailable: no default output device; input capture "
                "remains active."
            );
        } else {
            IRE_LOG_WARN(
                "Audio input monitoring unavailable: output device '{}' was not found; input "
                "capture remains active.",
                requestedDeviceName
            );
        }
        return false;
    }
    if (!report->channelsAvailable_ || !report->rateAvailable_) {
        m_monitorChannel->closeAdmission();
        IRE_LOG_WARN(
            "Audio input monitoring unavailable on output device '{}'; input capture remains "
            "active.",
            report->deviceName_
        );
        return false;
    }
    if (report->open_.outcome_ != CallOutcome::SUCCEEDED) {
        m_monitorChannel->closeAdmission();
        IRE_LOG_WARN(
            "Audio input monitoring unavailable: failed to open output device '{}': {}; input "
            "capture remains active.",
            report->deviceName_,
            callFailureText(report->open_)
        );
        return false;
    }
    if (!report->backendRateMatched_) {
        m_monitorChannel->closeAdmission();
        IRE_LOG_WARN(
            "Audio input monitoring unavailable: output device '{}' adjusted rate {} to {}; "
            "input capture remains active.",
            report->deviceName_,
            m_streamSampleRate,
            report->backendSampleRate_
        );
        return false;
    }
    if (report->start_.outcome_ != CallOutcome::SUCCEEDED) {
        m_monitorChannel->closeAdmission();
        IRE_LOG_WARN(
            "Audio input monitoring unavailable: failed to start output device '{}': {}; input "
            "capture remains active.",
            report->deviceName_,
            callFailureText(report->start_)
        );
        return false;
    }

    m_monitorStreamOpen = true;
    m_monitorStreamRunning = true;
    m_monitorDeviceName = report->deviceName_;
    m_monitorChannel->setOutputRunning(true);
    m_monitorChannel->openAdmission();
    const long inputLatencyFrames = m_session->streamLatencyFrames();
    const long outputLatencyFrames = m_monitorSession->streamLatencyFrames();
    const double inputLatencyMs =
        1000.0 * static_cast<double>(inputLatencyFrames) / m_streamSampleRate;
    const double outputLatencyMs =
        1000.0 * static_cast<double>(outputLatencyFrames) / m_streamSampleRate;
    const double queueLatencyMs =
        1000.0 * static_cast<double>(m_monitorChannel->targetFillFrames()) / m_streamSampleRate;
    const double elapsedMs = std::chrono::duration<double, std::milli>(report->elapsed_).count();
    IRE_LOG_INFO(
        "Opened audio monitor output: device='{}' sampleRate={} channels={} bufferFrames={} "
        "inputLatencyMs={:.3f} outputLatencyMs={:.3f} deviceRoundTripMs={:.3f} "
        "queueLatencyMs={:.3f} audibleLatencyMs={:.3f} outputOpenStartMs={:.3f}",
        report->deviceName_,
        m_streamSampleRate,
        m_streamChannels,
        report->bufferFrames_,
        inputLatencyMs,
        outputLatencyMs,
        inputLatencyMs + outputLatencyMs,
        queueLatencyMs,
        inputLatencyMs + outputLatencyMs + queueLatencyMs,
        elapsedMs
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
    const std::optional<CallReport> report =
        runBackendCall(*m_session, m_deadlines.arm_, [](detail::IAudioInputBackend &backend) {
            return backend.startStream();
        });
    if (!report) {
        const std::string deviceName = m_streamDeviceName;
        unpublishStreamIn();
        logBackendTimeout(kArmTimedOut, BackendOperation::START, m_deadlines.arm_, deviceName);
        return false;
    }
    if (logIfCallFailed("start", *report)) {
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
    const std::optional<CallReport> report =
        runBackendCall(*m_session, m_deadlines.teardown_, [](detail::IAudioInputBackend &backend) {
            return backend.stopStream();
        });
    if (!report) {
        const std::string deviceName = m_streamDeviceName;
        unpublishStreamIn();
        logBackendTimeout(
            kTeardownTimedOut,
            BackendOperation::STOP,
            m_deadlines.teardown_,
            deviceName
        );
        return;
    }
    if (logIfCallFailed("stop", *report)) {
        return;
    }
    m_streamInRunning = false;
}

void Audio::closeStreamIn() {
    const Clock::time_point deadline = Clock::now() + m_deadlines.teardown_;
    if (m_monitorChannel) {
        m_monitorChannel->closeAdmission();
    }
    closeStreamInBy(deadline);
    closeMonitorBy(deadline);
}

void Audio::closeStreamInBy(std::chrono::steady_clock::time_point deadline) {
    if (!m_streamInOpen) {
        return;
    }
    const bool stopFirst = m_streamInRunning;
    const std::string deviceName = m_streamDeviceName;
    unpublishStreamIn();

    auto report = std::make_shared<CloseReport>();
    if (stopFirst) {
        report->operation_ = BackendOperation::STOP;
    }
    const Completion completion = m_session->run(
        deadline,
        [report, stopFirst](detail::AudioInputBackendStream &stream, const std::atomic<bool> &) {
            detail::IAudioInputBackend &backend = *stream.backend_;
            if (stopFirst) {
                report->stop_ = callBackend(backend, [&] { return backend.stopStream(); });
                report->operation_ = BackendOperation::CLOSE;
            }
            // closeStream() returns no status and tears down a stream that is
            // still running, so the stream is closed even when the stop failed.
            try {
                backend.closeStream();
            } catch (...) {
                report->closeThrew_ = true;
            }
            stream.open_ = false;
        }
    );
    if (completion == Completion::TIMED_OUT) {
        logBackendTimeout(kTeardownTimedOut, report->operation_, m_deadlines.teardown_, deviceName);
        return;
    }
    if (stopFirst) {
        logIfCallFailed("stop", report->stop_);
    }
    if (report->closeThrew_) {
        IRE_LOG_ERROR("Failed to close audio input stream.");
    }
}

void Audio::closeMonitorBy(std::chrono::steady_clock::time_point deadline) {
    if (!m_monitorStreamOpen || !m_monitorSession) {
        return;
    }
    const bool stopFirst = m_monitorStreamRunning;
    const std::string deviceName = m_monitorDeviceName;
    const auto channel = m_monitorChannel;
    const std::uint64_t copiedFrames = channel->copiedFrames();
    const std::uint64_t outputCallbacks = channel->outputCallbacks();
    const std::uint64_t underrunFrames = channel->underrunFrames();
    const std::uint64_t overrunFrames = channel->overrunFrames();
    const std::uint64_t discardedFrames = channel->discardedFrames();
    unpublishMonitor();

    auto report = std::make_shared<CloseReport>();
    if (stopFirst) {
        report->operation_ = BackendOperation::STOP;
    }
    const Completion completion = m_monitorSession->run(
        deadline,
        [report, stopFirst](detail::AudioInputBackendStream &stream, const std::atomic<bool> &) {
            detail::IAudioInputBackend &backend = *stream.backend_;
            if (stopFirst) {
                report->stop_ = callBackend(backend, [&] { return backend.stopStream(); });
                report->operation_ = BackendOperation::CLOSE;
            }
            try {
                backend.closeStream();
            } catch (...) {
                report->closeThrew_ = true;
            }
            stream.open_ = false;
        }
    );
    if (completion == Completion::TIMED_OUT) {
        logMonitorTimeout(report->operation_, m_deadlines.teardown_, deviceName);
        return;
    }
    if (stopFirst && report->stop_.outcome_ != CallOutcome::SUCCEEDED) {
        IRE_LOG_WARN("Failed to stop audio monitor output stream '{}'.", deviceName);
    }
    if (report->closeThrew_) {
        IRE_LOG_WARN("Failed to close audio monitor output stream '{}'.", deviceName);
    }
    IRE_LOG_INFO(
        "Closed audio monitor output: device='{}' copiedFrames={} outputCallbacks={} "
        "underrunFrames={} overrunFrames={} discardedFrames={}",
        deviceName,
        copiedFrames,
        outputCallbacks,
        underrunFrames,
        overrunFrames,
        discardedFrames
    );
}

void Audio::unpublishStreamIn() {
    if (m_gate) {
        m_gate->close();
        m_gate.reset();
    }
    m_streamInOpen = false;
    m_streamInRunning = false;
    m_streamChannels = 0;
    m_streamDeviceName.clear();
}

void Audio::unpublishMonitor() {
    if (m_monitorChannel) {
        m_monitorChannel->closeAdmission();
    }
    m_monitorStreamOpen = false;
    m_monitorStreamRunning = false;
    m_monitorDeviceName.clear();
}

bool Audio::isStreamInOpen() const {
    return m_streamInOpen;
}

bool Audio::isStreamInRunning() const {
    return m_streamInRunning;
}

bool Audio::startCapture(const AudioCaptureConfig &config, AudioCaptureCallback cb) {
    const Clock::time_point deadline = Clock::now() + m_deadlines.arm_;
    const bool inputStarted = armStreamIn(
        config.device_name_,
        config.sample_rate_,
        config.channels_,
        std::move(cb),
        true,
        deadline
    );
    if (!inputStarted) {
        return false;
    }
    if (!config.monitor_enabled_) {
        return true;
    }
    if (config.device_name_.empty() && config.monitor_device_name_.empty()) {
        IRE_LOG_WARN(
            "Audio input monitoring uses the default input and output; microphone-to-speaker "
            "monitoring can create acoustic feedback."
        );
    }
    armMonitorBy(config, deadline);
    return true;
}

void Audio::stopCapture() {
    const Clock::time_point deadline = Clock::now() + m_deadlines.teardown_;
    if (m_monitorChannel) {
        m_monitorChannel->closeAdmission();
    }
    closeStreamInBy(deadline);
    closeMonitorBy(deadline);
}

bool Audio::isCapturing() const {
    return isStreamInRunning();
}

int Audio::getCaptureSampleRate() const {
    return m_streamInOpen ? m_streamSampleRate : 0;
}

double Audio::getInputLatencyMs() const {
    if (!m_streamInOpen || m_streamSampleRate <= 0) {
        return 0.0;
    }
    const long latencyFrames = m_session->streamLatencyFrames();
    return 1000.0 * static_cast<double>(latencyFrames) / static_cast<double>(m_streamSampleRate);
}

void Audio::setInputMonitorEnabled(bool enabled) {
    if (!m_monitorStreamRunning) {
        if (enabled) {
            IRE_LOG_WARN(
                "Cannot enable audio input monitoring: no monitor output stream is active."
            );
        }
        return;
    }
    m_monitorChannel->setRequestedEnabled(enabled);
}

bool Audio::isInputMonitorEnabled() const {
    return m_monitorChannel != nullptr && m_monitorChannel->effectiveEnabled();
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
            IRE_LOG_INFO("Sample rates: {}", formatSampleRates(info.sampleRates));
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
