#ifndef EVENT_PROFILER_H
#define EVENT_PROFILER_H

#include <irreden/ir_time.hpp>
#include <irreden/ir_profile.hpp>

constexpr int kUpdateTimeWarningThresholdMs = 10;

namespace IRTime {

template <> class EventProfiler<UPDATE> {
  public:
    EventProfiler()
        : m_start{}, m_timePointBeginEvent{}, m_lag{kFPSNanoDuration}, m_tickCount(0), m_sum{0},
          m_deltaTimeActual{0}, m_deltaTimeFixed{1.0 / static_cast<double>(IRConstants::kFPS)} {}

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
        if (MilliDuration(newTick).count() > kUpdateTimeWarningThresholdMs) {
            IRE_LOG_WARN("Update took {} ms", MilliDuration(newTick).count());
        }
        m_sum -= m_tickList[m_tickCount % kProfileHistoryBufferSize];
        m_sum += newTick;
        m_tickList[m_tickCount % kProfileHistoryBufferSize] = newTick;
        m_lag -= kFPSNanoDuration;

        m_tickCount++;

        // TODO: This is weird obvy
        if (m_tickCount % 6000 == 0) {
            IRE_LOG_DEBUG("Update FPS: {} ms", m_sum.count() / 10.0f);

            MilliDuration totalElapsedTime = current - m_start;
            IRE_LOG_INFO("Average fixed updates per second: {} updates",
                         m_tickCount * 1000.0f / totalElapsedTime.count());
        }
    }

    bool shouldUpdate() {
        return m_lag >= kFPSNanoDuration;
    }

    TimePoint getTimePointBeginEvent() const {
        return m_timePointBeginEvent;
    }

    // time in seconds
    double deltaTime() const {
        return m_deltaTimeActual.count();
    }

    double deltaTimeFixed() const {
        return m_deltaTimeFixed;
    }

  private:
    TimePoint m_start;
    TimePoint m_timePointBeginEvent;
    NanoDuration m_lag;
    MilliDuration m_tickList[kProfileHistoryBufferSize];
    MilliDuration m_sum;
    SecondsDuration m_deltaTimeActual;
    const double m_deltaTimeFixed;
    unsigned int m_tickCount;
};

template <> class EventProfiler<RENDER> {
  public:
    EventProfiler()
        : m_start{}, m_timePointBeginEvent{}, m_tickCount(0), m_sum{0}, m_deltaTimeActual{0} {}

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

    double deltaTimeSinceFixedUpdateStart(const EventProfiler<UPDATE> &updateProfiler) const {
        return (m_timePointBeginEvent - updateProfiler.getTimePointBeginEvent()).count();
    }

    void endEvent() {
        TimePoint current = Clock::now();
        auto newTick = current - m_timePointBeginEvent;
        m_sum -= m_tickList[m_tickCount % kProfileHistoryBufferSize];
        m_sum += newTick;
        m_tickList[m_tickCount % kProfileHistoryBufferSize] = newTick;

        m_tickCount++;

        if (m_tickCount % 1000 == 0) {
            IRE_LOG_DEBUG("Render FPS: {} ms", m_sum.count() / (float)kProfileHistoryBufferSize);

            MilliDuration totalElapsedTime = current - m_start;
            IRE_LOG_INFO("Average render calls per second: {} updates",
                         m_tickCount * 1000.0f / totalElapsedTime.count());
        }
    }

    TimePoint getTimePointBeginEvent() const {
        return m_timePointBeginEvent;
    }

  private:
    TimePoint m_start;
    TimePoint m_timePointBeginEvent;
    MilliDuration m_tickList[kProfileHistoryBufferSize];
    MilliDuration m_sum;
    SecondsDuration m_deltaTimeActual;
    unsigned int m_tickCount;
};

} // namespace IRTime

#endif /* EVENT_PROFILER_H */
