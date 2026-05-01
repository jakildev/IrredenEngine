#ifndef GPU_STAGE_TIMING_OBSERVER_H
#define GPU_STAGE_TIMING_OBSERVER_H

#include <irreden/ir_system.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <memory>
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
    void tagStage(IRSystem::SystemId system, const GpuStageInfo &info) {
        m_stages[system] = &info;
    }

    void onBeforeTick(IRSystem::SystemId system) override {
        if (!gpuStageTiming().enabled_) return;
        auto it = m_stages.find(system);
        if (it == m_stages.end()) return;
        IRRender::device()->finish();
        m_t0 = SteadyClock::now();
    }

    void onAfterTick(IRSystem::SystemId system) override {
        if (!gpuStageTiming().enabled_) return;
        auto it = m_stages.find(system);
        if (it == m_stages.end()) return;
        IRRender::device()->finish();
        gpuStageTiming().*(it->second->field_) = elapsedMs(m_t0, SteadyClock::now());
    }

  private:
    std::unordered_map<IRSystem::SystemId, const GpuStageInfo *> m_stages;
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
// future stage rename can't crash the engine at startup.
inline void tagGpuStage(IRSystem::SystemId system, std::string_view stageName) {
    for (const auto &info : gpuStageRegistry()) {
        if (info.name_ == stageName) {
            detail::installAndGetObserver()->tagStage(system, info);
            return;
        }
    }
}

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_OBSERVER_H */
