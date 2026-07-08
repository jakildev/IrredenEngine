#ifndef GPU_SUBSTAGE_TIMING_H
#define GPU_SUBSTAGE_TIMING_H

#include <irreden/render/render_device.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <array>
#include <string_view>

namespace IRRender {

// Intra-tick GPU sub-stage timing (#2280).
//
// Where the per-system `GpuStageTimingObserver` brackets a whole system tick
// with one timestamp pair, `GpuSubStageScope` brackets an individual dispatch
// group *inside* one tick, so a bundled per-system row can be split into its
// reserved sub-rows. VOXEL_TO_TRIXEL_STAGE_1 uses four of these
// (`canvasClear` / `voxelCompact` / `voxelStage1` / `voxelStage2`) to attribute
// what used to be one opaque `voxelStage1` measurement — see #2258.
//
// Mechanism: reuses the device timestamp-pair machinery unchanged
// (`createTimestampPair` / `writeTimestamp` / `readTimestampPairMs`) at the
// same attachment slot the observer uses. This is safe because a system that
// owns sub-scopes is NOT tagged for the per-system observer, so its own tick
// leaves that slot free. On Metal the sub-scope's `writeTimestamp(START)`
// claims the sticky encoder attachment for the enclosed dispatch group's
// encoders (compute encoders via `createComputeEncoder`, and the distance-clear
// blit via `createBlitEncoder`); `END` releases it — exactly the #1746
// semantics, scoped to the sub-window. On OpenGL the `glQueryCounter` markers
// bracket the enclosed commands in the stream.
//
// The per-scope async readback mirrors the observer's `kSamplesInFlight` ring,
// so no `finish()` stall is introduced. Timers-off (the default) costs one bool
// check per scope. A scope produces one sample per close; multi-canvas scenes
// overwrite the row with the last canvas's sample (the single-canvas #2258
// target is exact), matching every `GpuStageTiming::*Ms_` field's existing
// last-sample semantics.
class GpuSubStageTimer {
  public:
    static constexpr int kSamplesInFlight = 3;

    struct SubStageState {
        const GpuStageInfo *info_ = nullptr;
        int registryIndex_ = -1;
        std::array<GpuTimestampHandle, kSamplesInFlight> handles_{};
        std::array<bool, kSamplesInFlight> pending_{};
        int nextSlot_ = 0;
        int activeSlot_ = -1;
        bool initialized_ = false;
    };

    // Resolve @p stageName to its persistent per-stage state, lazily allocating
    // the timestamp-pair ring on first use. Returns nullptr for an unknown name
    // or when the device cannot allocate any pair (graceful no-op — the caller
    // just skips timing this scope, like the observer's invalid-handle path).
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
                RenderDevice *device = IRRender::device();
                if (device != nullptr && device->supportsGpuTimestampPairs()) {
                    const int pairs = IRMath::clamp(
                        device->recommendedTimestampPairsInFlight(),
                        1,
                        kSamplesInFlight
                    );
                    for (int p = 0; p < pairs; ++p) {
                        state.handles_[p] = device->createTimestampPair();
                    }
                }
                state.initialized_ = true;
            }
            return &state;
        }
        return nullptr;
    }

    void begin(SubStageState &state) {
        resolveReadySamples(state);
        const int slot = nextAvailableSlot(state);
        if (slot < 0) {
            state.activeSlot_ = -1;
            return;
        }
        state.activeSlot_ = slot;
        IRRender::device()->writeTimestamp(state.handles_[slot], TimestampSlot::START);
    }

    void end(SubStageState &state) {
        if (state.activeSlot_ < 0) {
            return;
        }
        IRRender::device()->writeTimestamp(state.handles_[state.activeSlot_], TimestampSlot::END);
        state.pending_[state.activeSlot_] = true;
        state.activeSlot_ = -1;
    }

  private:
    static void resolveReadySamples(SubStageState &state) {
        float ms = 0.0f;
        for (int i = 0; i < kSamplesInFlight; ++i) {
            if (!state.pending_[i]) {
                continue;
            }
            if (IRRender::device()->readTimestampPairMs(state.handles_[i], ms)) {
                commitGpuStageSample(*state.info_, state.registryIndex_, ms);
                state.pending_[i] = false;
            }
        }
    }

    static int nextAvailableSlot(SubStageState &state) {
        for (int attempt = 0; attempt < kSamplesInFlight; ++attempt) {
            const int slot = (state.nextSlot_ + attempt) % kSamplesInFlight;
            if (state.handles_[slot] != kInvalidGpuTimestampHandle && !state.pending_[slot]) {
                state.nextSlot_ = (slot + 1) % kSamplesInFlight;
                return slot;
            }
        }
        return -1;
    }

    // Persistent per-registry-stage state (parallel to `gpuStageRegistry()`).
    // Only the entries a `GpuSubStageScope` names are ever initialized. The
    // timer outlives every scope (program-bound singleton) so the pairs and the
    // in-flight ring survive across frames; the pairs leak at process exit,
    // which the OpenGL segfault-on-static-destruction lesson (#2031) makes the
    // deliberate choice over a static destructor that touches a dead GL context.
    std::array<SubStageState, kGpuStageCount> m_states{};
};

inline GpuSubStageTimer &gpuSubStageTimer() {
    static GpuSubStageTimer timer;
    return timer;
}

// RAII bracket for one intra-tick dispatch group. Construct just before the
// group's first dispatch/clear, let it destruct after the group's last barrier.
// A no-op unless per-frame GPU timing is enabled, the device supports timestamp
// pairs, and the legacy `finish()`-bracketed path is off (that path can't nest
// sub-scopes without a stall per group, so sub-rows stay 0.0f under it).
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
