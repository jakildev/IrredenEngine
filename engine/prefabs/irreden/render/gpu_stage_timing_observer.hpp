#ifndef GPU_STAGE_TIMING_OBSERVER_H
#define GPU_STAGE_TIMING_OBSERVER_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/profile/scope_timer.hpp>

#include <algorithm>
#include <memory>
#include <array>
#include <string_view>
#include <unordered_map>

namespace IRRender {

// Observer that brackets every system tick with GPU `device()->finish()`
// samples and writes the elapsed time into a `GpuStageTiming` field for
// systems that have been tagged via `tagGpuStage`. Centralizes what used
// to be 14 inlined `if (timing.enabled_) { device()->finish(); ... }`
// blocks scattered across `engine/prefabs/irreden/render/systems/*.hpp`.
//
// Untagged systems pay nothing: the `m_stages.find(system)` returns end
// and both fires return immediately. When `gpuStageTiming().enabled_` is
// false (the default), even tagged systems pay only one bool check per
// fire.
class GpuStageTimingObserver : public IRSystem::TickObserver {
  public:
    ~GpuStageTimingObserver() override {
        for (auto &[_, state] : m_stages) {
            if (state.device_ == nullptr) {
                continue;
            }
            for (GpuTimestampHandle handle : state.handles_) {
                if (handle != kInvalidGpuTimestampHandle) {
                    state.device_->destroyTimestampPair(handle);
                }
            }
        }
    }

    void tagStage(IRSystem::SystemId system, const GpuStageInfo &info) {
        StageState state{};
        state.info_ = &info;
        // Index into the parallel `gpuStageAccumulators()` array — `info` is a
        // reference into the `gpuStageRegistry()` array, so pointer arithmetic
        // against its base gives the matching accumulator slot.
        state.registryIndex_ = static_cast<int>(&info - gpuStageRegistry().data());
        state.device_ = IRRender::device();
        if (state.device_ != nullptr && state.device_->supportsGpuTimestampPairs()) {
            const int pairsInFlight =
                std::clamp(state.device_->recommendedTimestampPairsInFlight(), 1, kSamplesInFlight);
            for (int i = 0; i < pairsInFlight; ++i) {
                state.handles_[i] = state.device_->createTimestampPair();
            }
        }
        m_stages[system] = state;
    }

    void onBeforeTick(IRSystem::SystemId system) override {
        auto it = m_stages.find(system);
        if (it == m_stages.end())
            return;

        // CPU side is independent of GPU support / enable state — the histogram
        // gates itself when disabled. The wall-clock cost (two `now()` calls
        // + a hashmap lookup at scope exit) is well under a microsecond.
        if (IRProfile::cpuFrameHistogram().enabled_) {
            it->second.cpuT0_ = IRProfile::SteadyClock::now();
            it->second.cpuActive_ = true;
        } else {
            it->second.cpuActive_ = false;
        }

        if (!gpuStageTiming().enabled_)
            return;
        if (useLegacyTiming()) {
            IRRender::device()->finish();
            m_t0 = SteadyClock::now();
            return;
        }

        StageState &state = it->second;
        resolveReadySamples(state);
        const int slot = nextAvailableSlot(state);
        if (slot < 0) {
            state.activeSlot_ = -1;
            return;
        }
        state.activeSlot_ = slot;
        IRRender::device()->writeTimestamp(state.handles_[slot], TimestampSlot::START);
    }

    void onAfterTick(IRSystem::SystemId system) override {
        auto it = m_stages.find(system);
        if (it == m_stages.end())
            return;

        if (it->second.cpuActive_) {
            const auto cpuT1 = IRProfile::SteadyClock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(cpuT1 - it->second.cpuT0_).count();
            IRProfile::cpuFrameHistogram().record(it->second.info_->name_, ms);
            it->second.cpuActive_ = false;
        }

        if (!gpuStageTiming().enabled_)
            return;
        if (useLegacyTiming()) {
            IRRender::device()->finish();
            const float ms = elapsedMs(m_t0, SteadyClock::now());
            commitGpuStageSample(*it->second.info_, it->second.registryIndex_, ms);
            return;
        }

        StageState &state = it->second;
        if (state.activeSlot_ < 0) {
            return;
        }
        IRRender::device()->writeTimestamp(state.handles_[state.activeSlot_], TimestampSlot::END);
        state.pending_[state.activeSlot_] = true;
        state.activeSlot_ = -1;
    }

  private:
    static constexpr int kSamplesInFlight = 3;

    struct StageState {
        const GpuStageInfo *info_ = nullptr;
        int registryIndex_ = -1;
        std::array<GpuTimestampHandle, kSamplesInFlight> handles_{};
        std::array<bool, kSamplesInFlight> pending_{};
        RenderDevice *device_ = nullptr;
        int nextSlot_ = 0;
        int activeSlot_ = -1;

        // CPU sibling for the matching stage. Captured at `onBeforeTick`,
        // committed at `onAfterTick` regardless of `gpuStageTiming().enabled_`
        // — CPU timing is independent of GPU support / opt-in.
        IRProfile::TimePoint cpuT0_{};
        bool cpuActive_ = false;
    };

    static bool useLegacyTiming() {
        return gpuStageTiming().legacyFinishTiming_ ||
               !IRRender::device()->supportsGpuTimestampPairs();
    }

    static void resolveReadySamples(StageState &state) {
        float ms = 0.0f;
        for (int i = 0; i < kSamplesInFlight; ++i) {
            if (!state.pending_[i])
                continue;
            if (IRRender::device()->readTimestampPairMs(state.handles_[i], ms)) {
                commitGpuStageSample(*state.info_, state.registryIndex_, ms);
                state.pending_[i] = false;
            }
        }
    }

    static int nextAvailableSlot(StageState &state) {
        for (int attempt = 0; attempt < kSamplesInFlight; ++attempt) {
            const int slot = (state.nextSlot_ + attempt) % kSamplesInFlight;
            if (state.handles_[slot] != kInvalidGpuTimestampHandle && !state.pending_[slot]) {
                state.nextSlot_ = (slot + 1) % kSamplesInFlight;
                return slot;
            }
        }
        return -1;
    }

    std::unordered_map<IRSystem::SystemId, StageState> m_stages;
    TimePoint m_t0;
};

namespace detail {

// Lazy install: the first `tagGpuStage` call creates the observer,
// transfers ownership to SystemManager via `registerTickObserver`, and
// caches a raw pointer for subsequent tagging. The observer lives as
// long as the SystemManager (program-bound; see engine/world/CLAUDE.md).
inline GpuStageTimingObserver *installAndGetObserver() {
    static GpuStageTimingObserver *cached = []() {
        auto owner = std::make_unique<GpuStageTimingObserver>();
        GpuStageTimingObserver *raw = owner.get();
        IRSystem::registerTickObserver(std::move(owner));
        return raw;
    }();
    return cached;
}

} // namespace detail

// Tag a system with a stage name from `gpuStageRegistry()`. The registry
// is the single source of truth for the field pointer + budget share;
// callers pass only the name. An unknown name is a silent no-op so a
// future stage rename can't crash the engine at startup, but it emits a
// debug warning so registry/tag name drift is caught at tag time.
inline void tagGpuStage(IRSystem::SystemId system, std::string_view stageName) {
    for (const auto &info : gpuStageRegistry()) {
        if (info.name_ == stageName) {
            detail::installAndGetObserver()->tagStage(system, info);
            return;
        }
    }
    IRE_LOG_WARN(
        "tagGpuStage: unknown stage name \"{}\" — stage will not be timed. "
        "Check gpuStageRegistry() for the registered name.",
        stageName
    );
}

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_OBSERVER_H */
