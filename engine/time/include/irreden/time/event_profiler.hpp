#ifndef EVENT_PROFILER_H
#define EVENT_PROFILER_H

#include <irreden/ir_time.hpp>
#include <irreden/ir_profile.hpp>

constexpr int kUpdateTimeWarningThresholdMs = 10;
constexpr unsigned int kUpdateWarningCooldownTicks = 60;

namespace IRTime {

template <> class EventProfiler<UPDATE> {
  public:
    EventProfiler()
        : m_start{}
        , m_timePointBeginEvent{}
        , m_lag{kFPSNanoDuration}
        , m_tickCount(0)
        , m_sum{0}
        , m_deltaTimeActual{0}
        , m_deltaTimeFixed{1.0 / static_cast<double>(IRConstants::kFPS)}
        , m_fixedStepCount(0)
        , m_fpsHead(0)
        , m_fpsCount(0) {}

    void start() {
        auto current = Clock::now();
        m_start = current;
    }

    void addLag(NanoSeconds lag) {
        m_lag += lag;
    }

    NanoDuration getLag() const {
        return m_lag;
    }

    void beginEvent() {
        auto timePointPrevious = m_timePointBeginEvent;
        m_timePointBeginEvent = Clock::now();
        m_deltaTimeActual =
            std::chrono::duration_cast<Seconds>(m_timePointBeginEvent - timePointPrevious);
    }

    void endEvent() {
        TimePoint current = Clock::now();
        auto newTick = current - m_timePointBeginEvent;
        const auto tickMs = MilliDuration(newTick).count();
        if (tickMs > kUpdateTimeWarningThresholdMs) {
            if (m_warningLogCooldownTicks == 0) {
                IRE_LOG_WARN("Update took {} ms", tickMs);
                m_warningLogCooldownTicks = kUpdateWarningCooldownTicks;
            }
        } else {
            m_warningLogCooldownTicks = 0;
        }

        if (m_warningLogCooldownTicks > 0) {
            --m_warningLogCooldownTicks;
        }
        m_sum -= m_tickList[m_tickCount % kProfileHistoryBufferSize];
        m_sum += newTick;
        m_tickList[m_tickCount % kProfileHistoryBufferSize] = newTick;
        m_lag -= kFPSNanoDuration;
        ++m_fixedStepCount;

        recordFrame(current);
        m_tickCount++;
    }

    bool shouldUpdate() {
        return m_lag >= kFPSNanoDuration;
    }

    void skipEvent() {
        if (m_lag >= kFPSNanoDuration) {
            m_lag -= kFPSNanoDuration;
            ++m_fixedStepCount;
        }
    }

    TimePoint getTimePointBeginEvent() const {
        return m_timePointBeginEvent;
    }

    double deltaTime() const {
        return m_deltaTimeActual.count();
    }

    double deltaTimeFixed() const {
        return m_deltaTimeFixed;
    }

    double fps() const {
        if (m_fpsCount == 0) return 0.0;
        auto now = Clock::now();
        auto cutoff = now - std::chrono::seconds(1);
        size_t count = 0;
        for (size_t i = 0; i < m_fpsCount; ++i) {
            size_t idx = (m_fpsHead + kFpsWindowCapacity - 1 - i) % kFpsWindowCapacity;
            if (m_fpsTimestamps[idx] >= cutoff) ++count;
            else break;
        }
        return static_cast<double>(count);
    }

  private:
    void recordFrame(TimePoint tp) {
        m_fpsTimestamps[m_fpsHead] = tp;
        m_fpsHead = (m_fpsHead + 1) % kFpsWindowCapacity;
        if (m_fpsCount < kFpsWindowCapacity) m_fpsCount++;
    }

    TimePoint m_start;
    TimePoint m_timePointBeginEvent;
    NanoDuration m_lag;
    MilliDuration m_tickList[kProfileHistoryBufferSize];
    MilliDuration m_sum;
    SecondsDuration m_deltaTimeActual;
    const double m_deltaTimeFixed;
    unsigned int m_warningLogCooldownTicks = 0;
    unsigned int m_tickCount;
    unsigned int m_fixedStepCount;

    TimePoint m_fpsTimestamps[kFpsWindowCapacity];
    size_t m_fpsHead;
    size_t m_fpsCount;
};

template <> class EventProfiler<RENDER> {
  public:
    EventProfiler()
        : m_start{}
        , m_timePointBeginEvent{}
        , m_tickCount(0)
        , m_sum{0}
        , m_deltaTimeActual{0}
        , m_fpsHead(0)
        , m_fpsCount(0)
        , m_droppedFrameCount(0) {}

    void start() {
        auto current = Clock::now();
        m_start = current;
        m_timePointBeginEvent = current;
    }

    void beginEvent() {
        auto timePointPrevious = m_timePointBeginEvent;
        m_timePointBeginEvent = Clock::now();
        m_deltaTimeActual = m_timePointBeginEvent - timePointPrevious;
    }

    double deltaTime() const {
        return m_deltaTimeActual.count();
    }

    double frameTimeMs() const {
        return std::chrono::duration<double, std::milli>(m_deltaTimeActual).count();
    }

    double deltaTimeSinceFixedUpdateStart(const EventProfiler<UPDATE> &updateProfiler) const {
        return (m_timePointBeginEvent - updateProfiler.getTimePointBeginEvent()).count();
    }

    void endEvent() {
        TimePoint current = Clock::now();
        auto newTick = current - m_timePointBeginEvent;
        m_sum -= m_tickList[m_tickCount % kProfileHistoryBufferSize];
        m_sum += newTick;
        m_tickList[m_tickCount % kProfileHistoryBufferSize] = newTick;

        constexpr double kDropThresholdSeconds = 2.0 / static_cast<double>(IRConstants::kFPS);
        if (m_deltaTimeActual.count() > kDropThresholdSeconds) {
            ++m_droppedFrameCount;
        }

        recordFrame(current);
        m_tickCount++;
    }

    TimePoint getTimePointBeginEvent() const {
        return m_timePointBeginEvent;
    }

    double fps() const {
        if (m_fpsCount == 0) return 0.0;
        auto now = Clock::now();
        auto cutoff = now - std::chrono::seconds(1);
        size_t count = 0;
        for (size_t i = 0; i < m_fpsCount; ++i) {
            size_t idx = (m_fpsHead + kFpsWindowCapacity - 1 - i) % kFpsWindowCapacity;
            if (m_fpsTimestamps[idx] >= cutoff) ++count;
            else break;
        }
        return static_cast<double>(count);
    }

    unsigned int droppedFrames() const {
        return m_droppedFrameCount;
    }

    void resetDroppedFrames() {
        m_droppedFrameCount = 0;
    }

  private:
    void recordFrame(TimePoint tp) {
        m_fpsTimestamps[m_fpsHead] = tp;
        m_fpsHead = (m_fpsHead + 1) % kFpsWindowCapacity;
        if (m_fpsCount < kFpsWindowCapacity) m_fpsCount++;
    }

    TimePoint m_start;
    TimePoint m_timePointBeginEvent;
    MilliDuration m_tickList[kProfileHistoryBufferSize];
    MilliDuration m_sum;
    SecondsDuration m_deltaTimeActual;
    unsigned int m_tickCount;

    TimePoint m_fpsTimestamps[kFpsWindowCapacity];
    size_t m_fpsHead;
    size_t m_fpsCount;
    unsigned int m_droppedFrameCount;
};

} // namespace IRTime

#endif /* EVENT_PROFILER_H */
