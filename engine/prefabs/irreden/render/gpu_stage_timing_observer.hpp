#ifndef GPU_STAGE_TIMING_OBSERVER_H
#define GPU_STAGE_TIMING_OBSERVER_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/gpu_timestamp_ring.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/profile/scope_timer.hpp>

#include <memory>
#include <string_view>
#include <unordered_map>

namespace IRRender {

// Tagged systems collect CPU time independently of GPU timing. GPU counters
// bracket the whole tick; legacy timing instead measures CPU wall time around
// encoding and a finish(), after draining earlier GPU work.
class GpuStageTimingObserver : public IRSystem::TickObserver {
  public:
    ~GpuStageTimingObserver() override {
        for (auto &[_, state] : m_stages) {
            state.release(state.device_);
        }
    }

    void tagStage(IRSystem::SystemId system, const GpuStageInfo &info) {
        auto [it, inserted] = m_stages.try_emplace(system);
        StageState &state = it->second;
        if (!inserted) {
            if (state.info_ == &info) {
                return;
            }
            // Pending results belong to the previous stage, not the new label.
            state.release(state.device_);
            state = StageState{};
        }
        state.info_ = &info;
        // Index into the parallel `gpuStageAccumulators()` array — `info` is a
        // reference into the `gpuStageRegistry()` array, so pointer arithmetic
        // against its base gives the matching accumulator slot.
        state.registryIndex_ = static_cast<int>(&info - gpuStageRegistry().data());
        state.device_ = IRRender::device();
        state.initialize(state.device_);
    }

    void onBeforeTick(IRSystem::SystemId system) override {
        auto it = m_stages.find(system);
        if (it == m_stages.end())
            return;

        // CPU collection is independent of GPU support and timing mode.
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
        state.begin(*IRRender::device(), [&state](float ms) {
            commitGpuStageSample(*state.info_, state.registryIndex_, ms);
        });
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
        state.end(*IRRender::device());
    }

  private:
    struct StageState : detail::GpuTimestampRing {
        const GpuStageInfo *info_ = nullptr;
        int registryIndex_ = -1;
        RenderDevice *device_ = nullptr;

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

    std::unordered_map<IRSystem::SystemId, StageState> m_stages;
    TimePoint m_t0;
};

namespace detail {

inline GpuStageTimingObserver *installAndGetObserver() {
    auto &manager = IRSystem::getSystemManager();
    if (auto *observer = manager.findTickObserver<GpuStageTimingObserver>()) {
        return observer;
    }
    auto owner = std::make_unique<GpuStageTimingObserver>();
    auto *observer = owner.get();
    manager.registerTickObserver(std::move(owner));
    return observer;
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
