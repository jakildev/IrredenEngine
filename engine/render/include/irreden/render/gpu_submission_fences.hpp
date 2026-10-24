#ifndef GPU_SUBMISSION_FENCES_H
#define GPU_SUBMISSION_FENCES_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace IRRender::detail {

// A backend whose present() does not wait lets the CPU run at most this many
// presented submissions ahead of the GPU. Nothing else bounds it once no
// readback blocks: an unthrottled offscreen or swapInterval-0 GL loop queues
// frames until the driver stalls for hundreds of milliseconds at once, and
// every GpuReadbackRing overruns its slots. Two keep the GPU fed while the
// CPU encodes the next.
inline constexpr std::size_t kMaxPresentedSubmissionsInFlight = 2;

// Submission-serial bookkeeping for a backend that fences each presented
// submission with a pollable GPU object (an OpenGL timestamp query). Serial 1
// is the first submission; completedSerial() is the newest submission known
// to have finished on the GPU, so a reader holding serial S may consume S's
// results once completedSerial() >= S. Every fence still queued belongs to a
// submission newer than completedSerial(), oldest first, so inFlight() is the
// number of presented submissions the GPU has not finished.
//
// The backend supplies the fence objects and the two GPU queries:
// available(fence) answers without blocking, wait(fence) blocks until the
// fence's submission has finished. A retired fence goes to a free list for
// reuse instead of back to the driver.
template <typename Fence> class GpuSubmissionFences {
  public:
    std::uint64_t submissionSerial() const {
        return m_submissionSerial;
    }

    std::uint64_t completedSerial() const {
        return m_completedSerial;
    }

    std::size_t inFlight() const {
        return m_inFlight.size();
    }

    // A fence retired by an earlier poll, wait, or completeAll(), ready to
    // fence the next submission; empty when the backend must create one.
    std::optional<Fence> takeRetiredFence() {
        if (m_retired.empty()) {
            return std::nullopt;
        }
        const Fence fence = m_retired.back();
        m_retired.pop_back();
        return fence;
    }

    // @p fence now marks the end of the submission being recorded; the next
    // submission opens.
    void endSubmission(const Fence &fence) {
        m_inFlight.push_back({fence, m_submissionSerial});
        ++m_submissionSerial;
    }

    // Retires, oldest first, every fence @p available reports finished, and
    // stops at the first that is not: a later fence never retires ahead of
    // an earlier one.
    template <typename Available> std::uint64_t pollCompleted(Available available) {
        while (!m_inFlight.empty() && available(m_inFlight.front().fence_)) {
            retireOldest();
        }
        return m_completedSerial;
    }

    // Blocks, through @p wait on the oldest fence, until at most
    // @p maxInFlight submissions remain unfinished. A waited fence retires
    // directly; nothing re-polls it.
    template <typename Available, typename Wait>
    void throttle(std::size_t maxInFlight, Available available, Wait wait) {
        pollCompleted(available);
        while (m_inFlight.size() > maxInFlight) {
            wait(m_inFlight.front().fence_);
            retireOldest();
        }
    }

    // The backend drained the GPU: every submission so far, including the
    // one being recorded, has finished.
    void completeAll() {
        for (const InFlight &entry : m_inFlight) {
            m_retired.push_back(entry.fence_);
        }
        m_inFlight.clear();
        m_completedSerial = m_submissionSerial;
        ++m_submissionSerial;
    }

  private:
    struct InFlight {
        Fence fence_;
        std::uint64_t serial_;
    };

    void retireOldest() {
        m_completedSerial = m_inFlight.front().serial_;
        m_retired.push_back(m_inFlight.front().fence_);
        m_inFlight.pop_front();
    }

    std::deque<InFlight> m_inFlight;
    std::vector<Fence> m_retired;
    std::uint64_t m_submissionSerial = 1;
    std::uint64_t m_completedSerial = 0;
};

} // namespace IRRender::detail

#endif /* GPU_SUBMISSION_FENCES_H */
