#ifndef IR_RENDER_OFFSCREEN_FRAME_PACER_H
#define IR_RENDER_OFFSCREEN_FRAME_PACER_H

#include <chrono>

namespace IRRender {

// The deadline rule of the Metal OFFSCREEN pacer, driven by explicit time
// points so it is testable without a clock or a GPU. Each frame's deadline is
// one interval after the previous one. A frame that completes before its
// deadline sleeps to it, so a run of early frames holds the display cadence
// with no drift. A frame that completes at or after its deadline does not
// sleep and re-anchors the schedule at its completion, so the next frame gets
// a full interval rather than a burst that catches up with the old schedule.
class OffscreenFramePacer {
  public:
    using Clock = std::chrono::steady_clock;

    void reset(Clock::duration interval, Clock::time_point start) {
        m_interval = interval;
        m_lastDeadline = start;
    }

    // Returns the time to sleep until; equal to `now` when the frame is late.
    Clock::time_point frameCompleted(Clock::time_point now) {
        const Clock::time_point deadline = m_lastDeadline + m_interval;
        m_lastDeadline = deadline <= now ? now : deadline;
        return m_lastDeadline;
    }

    Clock::time_point lastDeadline() const {
        return m_lastDeadline;
    }

  private:
    Clock::duration m_interval{};
    Clock::time_point m_lastDeadline{};
};

} // namespace IRRender

#endif /* IR_RENDER_OFFSCREEN_FRAME_PACER_H */
