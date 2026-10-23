#ifndef GPU_READBACK_RING_H
#define GPU_READBACK_RING_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace IRRender::detail {

// Slot bookkeeping for GPU-side copies the CPU reads back without waiting.
// Each write records the RenderDevice::submissionSerial() that encodes its
// copy and the producer metadata the bytes are interpreted with; a slot is
// handed to the reader only once completedSubmissionSerial() has reached its
// serial. How many writes ago a slot was filled proves nothing: on Metal
// every write of a frame shares one command buffer that commits at present.
//
// A write reuses a drained slot and otherwise appends one, so the ring grows
// to the writes in flight (a frame's writes times the frames the backend
// queues) and loses no sample. The caller keeps one destination buffer per
// slot index and allocates it when slotCount() grows. At kMaxSlots the write
// replaces the oldest pending slot, dropping that sample rather than reading
// it early or waiting on it.
template <typename Producer, int kMaxSlots> struct GpuReadbackRing {
    struct Slot {
        Producer producer_{};
        std::uint64_t submission_ = 0;
        bool pending_ = false;
    };
    std::vector<Slot> slots_;

    GpuReadbackRing() {
        slots_.reserve(kMaxSlots);
    }

    int slotCount() const {
        return static_cast<int>(slots_.size());
    }

    // Returns the slot index whose buffer the caller copies into.
    int write(std::uint64_t submission, const Producer &producer) {
        int slot = 0;
        int oldest = 0;
        for (; slot < slotCount() && slots_[slot].pending_; ++slot) {
            if (slots_[slot].submission_ < slots_[oldest].submission_) {
                oldest = slot;
            }
        }
        if (slot == slotCount()) {
            if (slotCount() < kMaxSlots) {
                slots_.emplace_back();
            } else {
                slot = oldest;
            }
        }
        slots_[static_cast<std::size_t>(slot)] = Slot{producer, submission, true};
        return slot;
    }

    // Calls @p read(slot, producer) once for every pending slot whose
    // submission has completed.
    template <typename Read> void drainCompleted(std::uint64_t completedSubmission, Read read) {
        for (int slot = 0; slot < slotCount(); ++slot) {
            Slot &entry = slots_[static_cast<std::size_t>(slot)];
            if (!entry.pending_ || entry.submission_ > completedSubmission) {
                continue;
            }
            entry.pending_ = false;
            read(slot, static_cast<const Producer &>(entry.producer_));
        }
    }
};

} // namespace IRRender::detail

#endif /* GPU_READBACK_RING_H */
