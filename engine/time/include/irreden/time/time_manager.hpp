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
        // Fixed-step capture mode: during a headless --auto-screenshot run,
        // advance the UPDATE accumulator by exactly one fixed period per render
        // frame instead of real wall-clock elapsed. The capture window is
        // counted in render frames, but per-tick animation (AUTO_SPIN, etc.)
        // advances per UPDATE tick; without this, the uncapped (vsync-off) loop
        // races through the window in under one update period and the animation
        // is captured at ~identity and non-deterministically.
        m_profilerUpdate.addLag(m_fixedStep ? kFPSNanoDuration : m_mainLoopElapsed);
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

    void clampUpdateLag(uint32_t maxTicks) {
        m_profilerUpdate.clampLag(maxTicks);
    }

    /// Enable deterministic fixed-step UPDATE pacing: beginMainLoop feeds the
    /// UPDATE accumulator exactly one fixed period per render frame instead of
    /// real wall-clock elapsed. Used for headless --auto-screenshot capture so
    /// per-tick animation advances reproducibly. Off in interactive runs.
    void setFixedStep(bool enabled) {
        m_fixedStep = enabled;
    }

    template <Events event> double deltaTime();
    template <Events event> double fps();
    template <Events event> double frameTimeMs();

    unsigned int droppedFrames() const {
        return m_profilerRender.droppedFrames();
    }

    void resetDroppedFrames() {
        m_profilerRender.resetDroppedFrames();
    }

  private:
    EventProfiler<UPDATE> m_profilerUpdate;
    EventProfiler<RENDER> m_profilerRender;
    TimePoint m_start;
    TimePoint m_mainLoopPrevious;
    NanoSeconds m_mainLoopElapsed;
    bool m_fixedStep = false;
};

} // namespace IRTime

#endif /* TIME_MANAGER_H */
