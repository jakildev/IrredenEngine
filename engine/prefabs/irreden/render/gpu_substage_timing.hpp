#ifndef GPU_SUBSTAGE_TIMING_H
#define GPU_SUBSTAGE_TIMING_H

#include <irreden/render/gpu_timestamp_ring.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <array>
#include <string_view>

namespace IRRender {

// Substage scopes share the device's single timestamp attachment with the
// system observer, so their owning system must not also have a GPU timing tag.
// Busy rings skip invocations; only completed valid samples update statistics.
class GpuSubStageTimer {
  public:
    static constexpr int kSamplesInFlight = detail::GpuTimestampRing::kSamplesInFlight;

    struct SubStageState : detail::GpuTimestampRing {
        const GpuStageInfo *info_ = nullptr;
        int registryIndex_ = -1;
        bool initialized_ = false;
    };

    // Unknown names return nullptr. Allocation failures leave invalid slots;
    // a state with no valid slot remains inert.
    SubStageState *acquire(std::string_view stageName) {
        const auto &registry = gpuStageRegistry();
        for (std::size_t i = 0; i < registry.size(); ++i) {
            if (registry[i].name_ != stageName) {
                continue;
            }
            SubStageState &state = m_states[i];
            if (!state.initialized_) {
                state.info_ = &registry[i];
                state.registryIndex_ = static_cast<int>(i);
                state.initialize(IRRender::device());
                state.initialized_ = true;
            }
            return &state;
        }
        return nullptr;
    }

    void begin(SubStageState &state) {
        state.begin(*IRRender::device(), [&state](float ms) {
            commitGpuStageSample(*state.info_, state.registryIndex_, ms);
        });
    }

    void end(SubStageState &state) {
        state.end(*IRRender::device());
    }

  private:
    // Process-lifetime bookkeeping deliberately performs no GPU destruction:
    // the graphics device may already be gone during static teardown.
    std::array<SubStageState, kGpuStageCount> m_states{};
};

inline GpuSubStageTimer &gpuSubStageTimer() {
    static GpuSubStageTimer timer;
    return timer;
}

// RAII bracket for one intra-tick dispatch group. Construct just before the
// group's first dispatch/clear, let it destruct after the group's last barrier.
// A no-op unless per-frame GPU timing is enabled, the device supports timestamp
// pairs, and the `finish()`-bracketed compatibility path is off (it cannot nest
// sub-scopes without a stall per group, so no substage samples are collected under it).
class GpuSubStageScope {
  public:
    explicit GpuSubStageScope(std::string_view stageName) {
        const GpuStageTiming &timing = gpuStageTiming();
        if (!timing.enabled_ || timing.legacyFinishTiming_) {
            return;
        }
        RenderDevice *device = IRRender::device();
        if (device == nullptr || !device->supportsGpuTimestampPairs()) {
            return;
        }
        m_state = gpuSubStageTimer().acquire(stageName);
        if (m_state != nullptr) {
            gpuSubStageTimer().begin(*m_state);
        }
    }

    ~GpuSubStageScope() {
        if (m_state != nullptr) {
            gpuSubStageTimer().end(*m_state);
        }
    }

    GpuSubStageScope(const GpuSubStageScope &) = delete;
    GpuSubStageScope &operator=(const GpuSubStageScope &) = delete;
    GpuSubStageScope(GpuSubStageScope &&) = delete;
    GpuSubStageScope &operator=(GpuSubStageScope &&) = delete;

  private:
    GpuSubStageTimer::SubStageState *m_state = nullptr;
};

} // namespace IRRender

#endif /* GPU_SUBSTAGE_TIMING_H */
