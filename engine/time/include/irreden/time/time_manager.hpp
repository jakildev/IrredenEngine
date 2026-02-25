#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H

#include <irreden/time/ir_time_types.hpp>
#include <irreden/time/event_profiler.hpp>

namespace IRTime {

class TimeManager {
  public:
    TimeManager();
    ~TimeManager();

    void start() {
        auto current = Clock::now();

        m_mainLoopPrevious = current;
        m_start = current;
        m_profilerUpdate.start();
        m_profilerRender.start();
    }

    void beginMainLoop() {
        TimePoint current = Clock::now();
        m_mainLoopElapsed = current - m_mainLoopPrevious;
        m_profilerUpdate.addLag(m_mainLoopElapsed);
        m_mainLoopPrevious = current;
    }

    template <Events event> void beginEvent();

    template <Events event> void endEvent();

    bool shouldUpdate() {
        return m_profilerUpdate.shouldUpdate();
    }

    void skipUpdate() {
        m_profilerUpdate.skipEvent();
    }

    template <Events event> double deltaTime();

  private:
    EventProfiler<UPDATE> m_profilerUpdate;
    EventProfiler<RENDER> m_profilerRender;
    TimePoint m_start;
    TimePoint m_mainLoopPrevious;
    NanoSeconds m_mainLoopElapsed;
};

} // namespace IRTime

#endif /* TIME_MANAGER_H */
