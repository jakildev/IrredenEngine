#ifndef GPU_FRAME_TIMING_H
#define GPU_FRAME_TIMING_H

#include <cstdint>
#include <limits>

namespace IRRender {

struct GpuFrameTimingMetric {
    double totalMs_ = 0.0;
    double minMs_ = 0.0;
    double maxMs_ = 0.0;

    void add(double value, bool first) {
        totalMs_ += value;
        if (first || value < minMs_)
            minMs_ = value;
        if (value > maxMs_)
            maxMs_ = value;
    }
};

struct GpuFrameTimingStats {
    bool supported_ = false;
    std::uint64_t attemptedFrames_ = 0;
    std::uint64_t validFrames_ = 0;
    std::uint64_t invalidFrames_ = 0;
    std::uint64_t commandBuffers_ = 0;
    GpuFrameTimingMetric envelope_;
    GpuFrameTimingMetric commandBufferSpans_;
};

// Command-buffer spans contain GPU stalls; their sum is not GPU busy time.
// The frame envelope additionally contains gaps between separate submissions.
class GpuFrameTimingAccumulator {
  public:
    void reset(bool enabled, bool supported) {
        m_enabled = enabled && supported;
        m_active = false;
        m_stats = {};
        m_stats.supported_ = supported;
    }

    void beginFrame() {
        if (!m_enabled)
            return;
        if (m_active) {
            m_invalid = true;
            endFrame();
        }
        ++m_stats.attemptedFrames_;
        m_active = true;
        m_invalid = false;
        m_count = 0;
        m_firstStart = 0.0;
        m_lastEnd = 0.0;
        m_spanSum = 0.0;
    }

    void addCompletedBuffer(double startSeconds, double endSeconds, bool completed) {
        if (!m_active)
            return;
        if (!completed || !(startSeconds > 0.0 && endSeconds >= startSeconds &&
                            endSeconds < std::numeric_limits<double>::max())) {
            m_invalid = true;
            return;
        }
        if (m_count == 0 || startSeconds < m_firstStart)
            m_firstStart = startSeconds;
        if (endSeconds > m_lastEnd)
            m_lastEnd = endSeconds;
        m_spanSum += endSeconds - startSeconds;
        ++m_count;
    }

    void endFrame() {
        if (!m_active)
            return;
        m_active = false;
        if (m_invalid || m_count == 0) {
            ++m_stats.invalidFrames_;
            return;
        }
        const bool first = m_stats.validFrames_ == 0;
        m_stats.envelope_.add((m_lastEnd - m_firstStart) * 1000.0, first);
        m_stats.commandBufferSpans_.add(m_spanSum * 1000.0, first);
        m_stats.commandBuffers_ += m_count;
        ++m_stats.validFrames_;
    }

    const GpuFrameTimingStats &stats() const {
        return m_stats;
    }

  private:
    GpuFrameTimingStats m_stats;
    bool m_enabled = false;
    bool m_active = false;
    bool m_invalid = false;
    std::uint32_t m_count = 0;
    double m_firstStart = 0.0;
    double m_lastEnd = 0.0;
    double m_spanSum = 0.0;
};

} // namespace IRRender

#endif
