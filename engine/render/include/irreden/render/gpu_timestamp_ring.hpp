#ifndef GPU_TIMESTAMP_RING_H
#define GPU_TIMESTAMP_RING_H

#include <irreden/ir_math.hpp>
#include <irreden/render/render_device.hpp>

#include <array>

namespace IRRender::detail {

// Non-owning bookkeeping: the caller controls handle destruction while its
// device is alive. Pending pairs are never overwritten or synchronously waited on.
struct GpuTimestampRing {
    static constexpr int kSamplesInFlight = 3;
    std::array<GpuTimestampHandle, kSamplesInFlight> handles_{};
    std::array<bool, kSamplesInFlight> pending_{};
    int nextSlot_ = 0;
    int activeSlot_ = -1;

    void initialize(RenderDevice *device) {
        if (device == nullptr || !device->supportsGpuTimestampPairs()) {
            return;
        }
        const int count =
            IRMath::clamp(device->recommendedTimestampPairsInFlight(), 1, kSamplesInFlight);
        for (int i = 0; i < count; ++i) {
            handles_[i] = device->createTimestampPair();
        }
    }

    void release(RenderDevice *device) {
        if (device == nullptr) {
            return;
        }
        for (auto handle : handles_) {
            if (handle != kInvalidGpuTimestampHandle) {
                device->destroyTimestampPair(handle);
            }
        }
    }

    template <typename RecordSample> void begin(RenderDevice &device, RecordSample recordSample) {
        float ms = 0.0f;
        for (int i = 0; i < kSamplesInFlight; ++i) {
            if (!pending_[i]) {
                continue;
            }
            const auto status = device.pollTimestampPairMs(handles_[i], ms);
            if (status == TimestampReadStatus::READY) {
                recordSample(ms);
            }
            if (status != TimestampReadStatus::PENDING) {
                pending_[i] = false;
            }
        }
        activeSlot_ = -1;
        for (int attempt = 0; attempt < kSamplesInFlight; ++attempt) {
            const int slot = (nextSlot_ + attempt) % kSamplesInFlight;
            if (handles_[slot] == kInvalidGpuTimestampHandle || pending_[slot]) {
                continue;
            }
            activeSlot_ = slot;
            nextSlot_ = (slot + 1) % kSamplesInFlight;
            device.writeTimestamp(handles_[slot], TimestampSlot::START);
            return;
        }
    }

    void end(RenderDevice &device) {
        if (activeSlot_ < 0) {
            return;
        }
        device.writeTimestamp(handles_[activeSlot_], TimestampSlot::END);
        pending_[activeSlot_] = true;
        activeSlot_ = -1;
    }
};

} // namespace IRRender::detail

#endif /* GPU_TIMESTAMP_RING_H */
