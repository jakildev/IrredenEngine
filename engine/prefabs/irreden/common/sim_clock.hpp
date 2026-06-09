#ifndef IR_SIM_CLOCK_H
#define IR_SIM_CLOCK_H

// IRSim:: — the sim-clock / cycle / timer / stopwatch service. Reads and writes
// the C_SimClock singleton and queries the per-world C_Cycle / C_Timer /
// C_Stopwatch entities by name.
//
// Lives in engine/prefabs (header-only) rather than engine/time, even though the
// architect plan sketched it under engine/time: the service reads ECS components
// (engine/entity + the C_* prefab headers), and engine/time is a foundational
// static library that must not depend on the component layer. Only the raw
// always-advancing engine tick (IRTime::tick()) stays in engine/time; the
// pausable/scalable sim clock (IRSim::tick(), advanced by SYSTEM_SIM_CLOCK_ADVANCE
// at C_SimClock::timeScale_) lives here next to the components it owns.
//
// Name lookups (cycleFraction("day"), timerActive("reload"), ...) scan the
// matching component column linearly. Cycle/timer/stopwatch counts are small (a
// handful per world), so these are a query convenience, not a hot path; a system
// that needs per-frame cycle state reads the C_Cycle component directly via its
// dense archetype column instead.
//
// The C_SimClock singleton is lazily created on first access; a consumer that
// wants the clock to advance must (a) touch it once at init (any IRSim:: call
// does) and (b) register SYSTEM_SIM_CLOCK_ADVANCE in its UPDATE pipeline.

#include <irreden/common/components/component_cycle.hpp>
#include <irreden/common/components/component_sim_clock.hpp>
#include <irreden/common/components/component_stopwatch.hpp>
#include <irreden/common/components/component_timer.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace IRSim {

namespace detail {

// First entity whose component of type C carries `name`; nullptr if none.
template <typename C> C *findByName(std::string_view name) {
    C *found = nullptr;
    IREntity::forEachComponent<C>([&](C &c) {
        if (found == nullptr && c.name_ == name) {
            found = &c;
        }
    });
    return found;
}

} // namespace detail

// ---- Clock (C_SimClock singleton) -------------------------------------------

inline IRComponents::C_SimClock &clock() {
    return IREntity::singleton<IRComponents::C_SimClock>();
}

/// Current sim tick — the pausable/scalable simulation clock. Gameplay timers,
/// cooldowns, and day clocks read this; profilers/wall-clock infra read
/// IRTime::tick() instead.
inline std::uint64_t tick() {
    return clock().tickCount_;
}

inline float timeScale() {
    return clock().timeScale_;
}

inline bool isPaused() {
    return clock().timeScale_ == 0.0f;
}

/// Sets the sim time scale. Negative values are clamped to 0 (paused); the
/// SYSTEM_SIM_CLOCK_ADVANCE fast path keys on an exact 1.0.
inline void setTimeScale(float scale) {
    clock().timeScale_ = scale < 0.0f ? 0.0f : scale;
}

inline void pause() {
    setTimeScale(0.0f);
}

/// Resumes at normal (1x) speed. A caller that paused from a custom rate and
/// wants it back should re-apply setTimeScale itself (v1 keeps no saved rate).
inline void resume() {
    setTimeScale(1.0f);
}

// ---- Cycles (C_Cycle, by name) ----------------------------------------------

/// Which cycle index we are in (monotonic): (simTick + phaseOffset) / period.
inline std::uint64_t cycleNumber(std::string_view name) {
    const IRComponents::C_Cycle *c = detail::findByName<IRComponents::C_Cycle>(name);
    if (c == nullptr || c->periodTicks_ == 0) {
        return 0;
    }
    return (tick() + c->phaseOffset_) / c->periodTicks_;
}

/// Tick offset inside the current cycle: (simTick + phaseOffset) % period.
inline std::uint64_t cycleTickWithin(std::string_view name) {
    const IRComponents::C_Cycle *c = detail::findByName<IRComponents::C_Cycle>(name);
    if (c == nullptr || c->periodTicks_ == 0) {
        return 0;
    }
    return (tick() + c->phaseOffset_) % c->periodTicks_;
}

/// Progress through the current cycle in [0, 1). The load-bearing primitive for
/// continuous time-driven values (sun angle, light color temp, shader uniforms):
/// read it as `float t = IRSim::cycleFraction("day")` and drive anything on a
/// [0, 1] parameterization. Wraps to 0 at each boundary.
inline float cycleFraction(std::string_view name) {
    const IRComponents::C_Cycle *c = detail::findByName<IRComponents::C_Cycle>(name);
    if (c == nullptr || c->periodTicks_ == 0) {
        return 0.0f;
    }
    const std::uint64_t within = (tick() + c->phaseOffset_) % c->periodTicks_;
    return static_cast<float>(within) / static_cast<float>(c->periodTicks_);
}

/// True only on the tick SYSTEM_CYCLE_BOUNDARY_DETECT crossed a period boundary
/// for this cycle. The poll form of the discrete boundary event (Lua polls it
/// here in lieu of a callback; a C++ consumer ordered after the detector can
/// read the same flag off the C_Cycle column directly).
inline bool cycleBoundaryCrossed(std::string_view name) {
    const IRComponents::C_Cycle *c = detail::findByName<IRComponents::C_Cycle>(name);
    return c != nullptr && c->boundaryCrossed_;
}

// ---- Timers (C_Timer, by name) ----------------------------------------------

inline bool timerActive(std::string_view name) {
    const IRComponents::C_Timer *t = detail::findByName<IRComponents::C_Timer>(name);
    return t != nullptr && t->active_;
}

/// Sim ticks until the timer fires; 0 once due or inactive.
inline std::uint64_t timerTicksRemaining(std::string_view name) {
    const IRComponents::C_Timer *t = detail::findByName<IRComponents::C_Timer>(name);
    if (t == nullptr || !t->active_) {
        return 0;
    }
    const std::uint64_t now = tick();
    return t->targetTick_ > now ? t->targetTick_ - now : 0;
}

/// Progress toward the current target in [0, 1], anchored at the timer's
/// countdown start (set at creation, re-anchored on each recurring re-arm).
inline float timerFraction(std::string_view name) {
    const IRComponents::C_Timer *t = detail::findByName<IRComponents::C_Timer>(name);
    if (t == nullptr) {
        return 0.0f;
    }
    if (t->targetTick_ <= t->startTick_) {
        return 1.0f; // degenerate / already-due span reads as complete
    }
    const std::uint64_t now = tick();
    const std::uint64_t span = t->targetTick_ - t->startTick_;
    const std::uint64_t done = now > t->startTick_ ? now - t->startTick_ : 0;
    return IRMath::clamp(static_cast<float>(done) / static_cast<float>(span), 0.0f, 1.0f);
}

/// True only on the tick SYSTEM_TIMER_FIRE fired this timer. The poll form of
/// the discrete fired event (Lua polls it here in lieu of a callback).
inline bool timerFired(std::string_view name) {
    const IRComponents::C_Timer *t = detail::findByName<IRComponents::C_Timer>(name);
    return t != nullptr && t->fired_;
}

// ---- Stopwatches (C_Stopwatch, by name) -------------------------------------

/// Elapsed sim ticks since start/reset, excluding paused spans.
inline std::uint64_t stopwatchElapsed(std::string_view name) {
    const IRComponents::C_Stopwatch *s = detail::findByName<IRComponents::C_Stopwatch>(name);
    if (s == nullptr) {
        return 0;
    }
    if (!s->running_) {
        return s->pausedElapsed_;
    }
    const std::uint64_t now = tick();
    const std::uint64_t segment = now > s->startTick_ ? now - s->startTick_ : 0;
    return s->pausedElapsed_ + segment;
}

inline bool stopwatchRunning(std::string_view name) {
    const IRComponents::C_Stopwatch *s = detail::findByName<IRComponents::C_Stopwatch>(name);
    return s != nullptr && s->running_;
}

inline void stopwatchPause(std::string_view name) {
    IRComponents::C_Stopwatch *s = detail::findByName<IRComponents::C_Stopwatch>(name);
    if (s == nullptr || !s->running_) {
        return;
    }
    const std::uint64_t now = tick();
    s->pausedElapsed_ += now > s->startTick_ ? now - s->startTick_ : 0;
    s->running_ = false;
}

inline void stopwatchResume(std::string_view name) {
    IRComponents::C_Stopwatch *s = detail::findByName<IRComponents::C_Stopwatch>(name);
    if (s == nullptr || s->running_) {
        return;
    }
    s->startTick_ = tick();
    s->running_ = true;
}

inline void stopwatchReset(std::string_view name) {
    IRComponents::C_Stopwatch *s = detail::findByName<IRComponents::C_Stopwatch>(name);
    if (s == nullptr) {
        return;
    }
    s->startTick_ = tick();
    s->pausedElapsed_ = 0;
}

// ---- Factories (snapshot the live sim tick where it matters) -----------------

inline IREntity::EntityId
createCycle(std::string name, std::uint64_t periodTicks, std::uint64_t phaseOffset = 0) {
    return IREntity::createEntity(
        IRComponents::C_Cycle{std::move(name), periodTicks, phaseOffset}
    );
}

/// `targetTick` is an absolute sim tick (the timer fires when IRSim::tick()
/// reaches it). startTick_ is anchored to the current sim tick so timerFraction
/// reports progress over the whole countdown.
inline IREntity::EntityId
createTimer(std::string name, std::uint64_t targetTick, std::uint64_t intervalTicks = 0) {
    IRComponents::C_Timer timer{std::move(name), targetTick, intervalTicks};
    timer.startTick_ = tick();
    return IREntity::createEntity(timer);
}

inline IREntity::EntityId createStopwatch(std::string name) {
    return IREntity::createEntity(IRComponents::C_Stopwatch{std::move(name), tick()});
}

} // namespace IRSim

#endif /* IR_SIM_CLOCK_H */
