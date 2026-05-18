#ifndef IR_PROFILE_SCOPE_TIMER_H
#define IR_PROFILE_SCOPE_TIMER_H

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace IRProfile {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = SteadyClock::time_point;

struct CpuScopeStats {
    double totalMs_ = 0.0;
    double maxMs_ = 0.0;
    std::uint32_t count_ = 0;
};

namespace detail {

// Transparent hash + equality let `find(std::string_view)` work without
// allocating a `std::string` on every lookup. Required by the engine rule
// that profiling overhead must stay bounded — once the map has warmed up
// after the first few frames there are no further allocations on the hot
// path.
struct TransparentStringHash {
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
    std::size_t operator()(const std::string &s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
    std::size_t operator()(const char *s) const noexcept {
        return std::hash<std::string_view>{}(s);
    }
};

struct TransparentStringEqual {
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

using CpuScopeMap = std::unordered_map<
    std::string,
    CpuScopeStats,
    TransparentStringHash,
    TransparentStringEqual>;

} // namespace detail

// Per-frame CPU scope histogram. `IR_PROFILE_SCOPE(name)` instances record
// into the "current" map at scope exit; `endFrame()` swaps current → last so
// the HUD can read a stable per-frame snapshot. Single-threaded by
// construction — the engine's tick fires on the main thread only.
class CpuFrameHistogram {
  public:
    bool enabled_ = false;

    void record(std::string_view name, double ms) {
        if (!enabled_)
            return;
        auto it = m_current.find(name);
        if (it == m_current.end()) {
            it = m_current.emplace(std::string{name}, CpuScopeStats{}).first;
        }
        it->second.totalMs_ += ms;
        // Tiny manual max keeps engine/profile/ independent of engine/math/
        // (engine/profile/ is a low-level module — pulling IRMath in just
        // for a two-arg max would invert the dependency direction).
        if (ms > it->second.maxMs_) {
            it->second.maxMs_ = ms;
        }
        ++it->second.count_;
    }

    void endFrame() {
        m_lastFrame.swap(m_current);
        for (auto &[_, stats] : m_current) {
            stats = CpuScopeStats{};
        }
    }

    double lastFrameMs(std::string_view name) const {
        auto it = m_lastFrame.find(name);
        return it == m_lastFrame.end() ? 0.0 : it->second.totalMs_;
    }

    const detail::CpuScopeMap &lastFrame() const { return m_lastFrame; }

  private:
    detail::CpuScopeMap m_current;
    detail::CpuScopeMap m_lastFrame;
};

inline CpuFrameHistogram &cpuFrameHistogram() {
    static CpuFrameHistogram instance;
    return instance;
}

class ScopeTimer {
  public:
    explicit ScopeTimer(std::string_view name)
        : m_name{name}, m_enabled{cpuFrameHistogram().enabled_} {
        if (m_enabled) {
            m_t0 = SteadyClock::now();
        }
    }

    ~ScopeTimer() {
        if (!m_enabled)
            return;
        const auto t1 = SteadyClock::now();
        const double ms = std::chrono::duration<double, std::milli>(t1 - m_t0).count();
        cpuFrameHistogram().record(m_name, ms);
    }

    ScopeTimer(const ScopeTimer &) = delete;
    ScopeTimer &operator=(const ScopeTimer &) = delete;

  private:
    std::string_view m_name;
    TimePoint m_t0{};
    bool m_enabled;
};

} // namespace IRProfile

#ifndef IR_RELEASE
#define IR_PROFILE_SCOPE_CONCAT_INNER(a, b) a##b
#define IR_PROFILE_SCOPE_CONCAT(a, b) IR_PROFILE_SCOPE_CONCAT_INNER(a, b)
#define IR_PROFILE_SCOPE(name)                                                                     \
    ::IRProfile::ScopeTimer IR_PROFILE_SCOPE_CONCAT(ir_profile_scope_, __LINE__) { name }
#else
#define IR_PROFILE_SCOPE(name)
#endif

#endif /* IR_PROFILE_SCOPE_TIMER_H */
