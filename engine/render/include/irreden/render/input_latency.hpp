#ifndef IR_RENDER_INPUT_LATENCY_H
#define IR_RENDER_INPUT_LATENCY_H

#include <irreden/profile/profile_report.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace IRRender {

using IRProfile::FrameEndEvent;

/// Seconds on `std::chrono::steady_clock`, the one clock every input latency
/// timestamp is expressed on.
inline double steadyClockSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// One submitted frame the device has finished with. Times are steady-clock
/// seconds. `endSeconds_` comes from the GPU or the presentation layer, never
/// from the moment the CPU noticed; `observedSeconds_` is that moment.
/// `valid_` is false when the frame finished without a usable end time.
struct FrameCompletion {
    std::uint64_t frameId_ = 0;
    double submitSeconds_ = 0.0;
    double endSeconds_ = 0.0;
    double observedSeconds_ = 0.0;
    double mappingErrorSeconds_ = 0.0;
    bool valid_ = false;
};

namespace detail {

/// Fixed-capacity FIFO. A push into a full ring drops the oldest entry.
template <typename T, std::size_t N> class LatencyRing {
  public:
    void clear() {
        m_head = 0;
        m_count = 0;
    }
    std::size_t size() const {
        return m_count;
    }
    bool empty() const {
        return m_count == 0;
    }
    T &front() {
        return m_items[m_head];
    }
    T &operator[](std::size_t i) {
        return m_items[(m_head + i) % N];
    }
    void push(const T &item) {
        if (m_count == N) {
            pop();
        }
        m_items[(m_head + m_count) % N] = item;
        ++m_count;
    }
    void pop() {
        m_head = (m_head + 1) % N;
        --m_count;
    }

  private:
    std::array<T, N> m_items{};
    std::size_t m_head = 0;
    std::size_t m_count = 0;
};

} // namespace detail

/// The render device's side of the probe: which frame each submission belongs
/// to, and when it finished. Submissions finish in order. Disabled, or on a
/// device that reports no frame end, every call returns without reading a
/// clock.
class FrameCompletionQueue {
  public:
    static constexpr std::size_t kCapacity = 8;

    void reset(bool enabled, FrameEndEvent event) {
        m_event = event;
        m_enabled = enabled && event != FrameEndEvent::NOT_MEASURED;
        m_inFlight.clear();
        m_finished.clear();
    }
    bool enabled() const {
        return m_enabled;
    }
    FrameEndEvent endEvent() const {
        return m_event;
    }
    /// The frame the next submission belongs to.
    void setFrameId(std::uint64_t frameId) {
        m_frameId = frameId;
    }
    std::uint64_t frameId() const {
        return m_frameId;
    }
    template <typename Clock> void submitted(const Clock &now) {
        if (!m_enabled) {
            return;
        }
        FrameCompletion submission;
        submission.frameId_ = m_frameId;
        submission.submitSeconds_ = now();
        m_inFlight.push(submission);
    }
    /// The oldest in-flight submission finished.
    void
    finished(double endSeconds, double observedSeconds, double mappingErrorSeconds, bool valid) {
        if (!m_enabled || m_inFlight.empty()) {
            return;
        }
        FrameCompletion completion = m_inFlight.front();
        m_inFlight.pop();
        completion.endSeconds_ = endSeconds;
        completion.observedSeconds_ = observedSeconds;
        completion.mappingErrorSeconds_ = mappingErrorSeconds;
        completion.valid_ = valid;
        m_finished.push(completion);
    }
    /// Never blocks: false when no finished frame is waiting to be taken.
    bool poll(FrameCompletion &out) {
        if (m_finished.empty()) {
            return false;
        }
        out = m_finished.front();
        m_finished.pop();
        return true;
    }
    std::uint32_t inFlight() const {
        return static_cast<std::uint32_t>(m_inFlight.size());
    }

  private:
    detail::LatencyRing<FrameCompletion, kCapacity> m_inFlight;
    detail::LatencyRing<FrameCompletion, kCapacity> m_finished;
    std::uint64_t m_frameId = 0;
    FrameEndEvent m_event = FrameEndEvent::NOT_MEASURED;
    bool m_enabled = false;
};

/// The frame loop's side of the probe. It holds the last OS-event poll, opens
/// one record per RENDER, and closes it when the device reports that frame's
/// end, which may arrive frames later. Records stay parallel to the profile
/// report's frame series: one per RENDER, ended or not. Disabled, no method
/// reads a clock or writes a record.
class InputLatencyAccumulator {
  public:
    static constexpr std::size_t kPendingCapacity = 8;

    void reset(bool enabled, std::size_t reserveFrames) {
        m_enabled = enabled;
        m_frames.clear();
        if (enabled) {
            m_frames.reserve(reserveFrames);
        }
        m_pending.clear();
        m_hasPoll = false;
        m_frameStartSeconds = 0.0;
        m_framesInFlight = 0;
        m_mappingErrorMs = 0.0;
    }
    bool enabled() const {
        return m_enabled;
    }
    template <typename Clock> void recordPoll(const Clock &now) {
        if (!m_enabled) {
            return;
        }
        m_lastPollSeconds = now();
        m_hasPoll = true;
    }
    /// The frame's CPU work starts with @p framesInFlight submitted frames
    /// still unfinished.
    template <typename Clock> void beginFrame(std::uint32_t framesInFlight, const Clock &now) {
        if (!m_enabled) {
            return;
        }
        m_frameStartSeconds = now();
        m_framesInFlight = framesInFlight;
    }
    /// RENDER sampled the cursor. A frame with no poll of its own keeps the
    /// most recent earlier poll; a frame before any poll records no reading.
    template <typename Clock> void recordRender(std::uint64_t frameId, const Clock &now) {
        if (!m_enabled) {
            return;
        }
        const double cursorSeconds = now();
        IRProfile::InputLatencyFrame frame;
        frame.framesInFlight_ = m_framesInFlight;
        m_frames.push_back(frame);
        if (m_hasPoll) {
            m_pending.push(
                {frameId,
                 m_frames.size() - 1,
                 m_frameStartSeconds,
                 m_lastPollSeconds,
                 cursorSeconds}
            );
        }
    }
    /// A completion for a frame no longer pending (never recorded, or
    /// dropped from a full pending ring) is ignored. Older pending frames
    /// are given up: submissions finish in order, so theirs never arrives.
    void addCompletion(const FrameCompletion &completion) {
        while (!m_pending.empty() && m_pending.front().frameId_ < completion.frameId_) {
            m_pending.pop();
        }
        if (m_pending.empty() || m_pending.front().frameId_ != completion.frameId_) {
            return;
        }
        const Pending pending = m_pending.front();
        m_pending.pop();
        if (!completion.valid_) {
            return;
        }
        const auto toMs = [](double seconds) { return static_cast<float>(seconds * 1000.0); };
        IRProfile::InputLatencyFrame &frame = m_frames[pending.frameIndex_];
        frame.inputToEndMs_ = toMs(completion.endSeconds_ - pending.pollSeconds_);
        frame.cursorToEndMs_ = toMs(completion.endSeconds_ - pending.cursorSeconds_);
        frame.submitToEndMs_ = toMs(completion.endSeconds_ - completion.submitSeconds_);
        frame.startToSubmitMs_ = toMs(completion.submitSeconds_ - pending.frameStartSeconds_);
        frame.endObservedLagMs_ = toMs(completion.observedSeconds_ - completion.endSeconds_);
        frame.ended_ = true;
        const double mappingErrorMs = completion.mappingErrorSeconds_ * 1000.0;
        if (mappingErrorMs > m_mappingErrorMs) {
            m_mappingErrorMs = mappingErrorMs;
        }
    }
    const std::vector<IRProfile::InputLatencyFrame> &frames() const {
        return m_frames;
    }
    /// Moves the records out. A disabled probe reports NOT_MEASURED.
    IRProfile::InputLatencySummary takeSummary(FrameEndEvent event) {
        IRProfile::InputLatencySummary summary;
        if (!m_enabled) {
            return summary;
        }
        summary.endEvent_ = event;
        summary.frames_ = std::move(m_frames);
        summary.clockMappingErrorMs_ = m_mappingErrorMs;
        m_frames.clear();
        return summary;
    }

  private:
    struct Pending {
        std::uint64_t frameId_ = 0;
        std::size_t frameIndex_ = 0;
        double frameStartSeconds_ = 0.0;
        double pollSeconds_ = 0.0;
        double cursorSeconds_ = 0.0;
    };

    std::vector<IRProfile::InputLatencyFrame> m_frames;
    detail::LatencyRing<Pending, kPendingCapacity> m_pending;
    double m_frameStartSeconds = 0.0;
    double m_lastPollSeconds = 0.0;
    double m_mappingErrorMs = 0.0;
    std::uint32_t m_framesInFlight = 0;
    bool m_enabled = false;
    bool m_hasPoll = false;
};

} // namespace IRRender

#endif /* IR_RENDER_INPUT_LATENCY_H */
