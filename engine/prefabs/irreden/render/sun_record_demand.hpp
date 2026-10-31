#ifndef SUN_RECORD_DEMAND_H
#define SUN_RECORD_DEMAND_H

#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/render_device.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/gpu_readback_ring.hpp>
#include <irreden/render/sun_face_query_layout.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace IRPrefab::SunShadow {

// One completed demand sample: the records a frame requested and the capacity
// of the pool generation that frame indexed into. The pool may have been
// resized since, so exhaustion is judged against capacity_, never the live pool.
struct SourceFaceDemandSample {
    std::uint64_t frame_ = 0;
    std::uint32_t capacity_ = 0;
    std::uint32_t requested_ = 0;

    bool exhausted() const {
        return requested_ > capacity_;
    }
};

// Copies each frame's requested-record count off the GPU timeline into its
// own readback buffer and hands it back once the submission that encoded the
// copy has completed. Frame age proves nothing: on Metal a frame's copies
// share one uncommitted command buffer, and getSubData reads shared storage
// without waiting.
class SourceFaceDemandRing {
  public:
    static constexpr int kSlots = 4;

    // Encodes the copy of @p pool's header word; @p capacity is the pool
    // generation's capacity. The pool's producer writes must precede this call
    // behind a BUFFER_UPDATE barrier.
    void sample(const IRRender::Buffer *pool, std::uint32_t capacity) {
        const int slot =
            m_ring.write(IRRender::device()->submissionSerial(), {++m_frames, capacity});
        if (slot == static_cast<int>(m_buffers.size())) {
            m_buffers.push_back(
                std::make_unique<IRRender::Buffer>(
                    nullptr,
                    sizeof(std::uint32_t),
                    IRRender::BUFFER_STORAGE_DYNAMIC
                )
            );
        }
        IRRender::device()->copyBufferSubData(
            pool,
            std::size_t(kSourceFaceHeaderOffset) * sizeof(std::uint32_t),
            m_buffers[static_cast<std::size_t>(slot)].get(),
            0,
            sizeof(std::uint32_t)
        );
    }

    // Calls @p read(sample) for every slot whose submission is at or below
    // @p completedSubmission. Production passes completedSubmissionSerial().
    template <typename Read> void drainCompleted(std::uint64_t completedSubmission, Read read) {
        m_ring.drainCompleted(completedSubmission, [&](int slot, const Producer &producer) {
            SourceFaceDemandSample sample{producer.frame_, producer.capacity_, 0};
            m_buffers[static_cast<std::size_t>(slot)]
                ->getSubData(0, sizeof(sample.requested_), &sample.requested_);
            read(static_cast<const SourceFaceDemandSample &>(sample));
        });
    }

    // Frames sampled so far; the next sample() is frame frames() + 1.
    std::uint64_t frames() const {
        return m_frames;
    }

  private:
    struct Producer {
        std::uint64_t frame_ = 0;
        std::uint32_t capacity_ = 0;
    };
    IRRender::detail::GpuReadbackRing<Producer, kSlots> m_ring{};
    std::vector<std::unique_ptr<IRRender::Buffer>> m_buffers;
    std::uint64_t m_frames = 0;
};

} // namespace IRPrefab::SunShadow

#endif /* SUN_RECORD_DEMAND_H */
