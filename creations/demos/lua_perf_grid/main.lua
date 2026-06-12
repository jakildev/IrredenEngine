-- Lua-driven ECS perf-parity demo. The schema + tick body below are the
-- single source of truth: cmake/lua_codegen reads this file at build time
-- and emits a C++ component struct + a typed System<...> specialisation
-- whose body is the tick converted to plain C++. At runtime, the same
-- file is loaded again; IRComponent.register is idempotent against the
-- codegen pre-bind and IRSystem.registerSystem no-ops the unmarked call
-- when EcsDefaultMode == CODEGEN. Under -DIR_LUA_ECS_DEFAULT_MODE=EVAL
-- the same call registers via the LuaJIT EVAL path.
--
-- LOGIC PARITY WITH C_PeriodicIdle.
--
-- This component + tick mirror engine/prefabs/irreden/update/components/
-- component_periodic_idle.hpp and engine/prefabs/irreden/update/systems/
-- system_periodic_idle.hpp for the 2-stage SineEaseInOut wave used by
-- perf_grid: same field set, same per-tick arithmetic, same branch
-- structure (pause / cycle-wrap / stage-advance / map+ease+lerp +
-- amplitude). The codegen DSL is restricted to flat scalars, so:
--   - `vec3 amplitude_` is flattened to amp_x/amp_y/amp_z;
--   - `vec3 m_currentValue` (the eased output) is out_x/out_y/out_z;
--   - `std::vector<PeriodStage> stages_` is hardcoded as two flat
--     stages (s0_* and s1_*) — perf_grid only uses two stages, both
--     SineEaseInOut, which the tick body inlines as
--       eased = -0.5 * (cos(pi * t) - 1)   -- glm::sineEaseInOut<float>
--   - `m_angleIncrementPerTick` is angle_inc, precomputed C++-side at
--     construction (2*pi / period / kFPS), mirroring the C++
--     constructor's `m_angleIncrementPerTick{...}` initialiser.
--
-- Bug-for-bug parity includes: `mapAngleToStageTValue` computes
-- `clampedAngle` but then uses the unclamped `angle` for the relative
-- position — preserved here.
--
-- Field order on the codegen-emitted struct + `LuaWaveState.new(...)`
-- constructor is alphabetical (T-106 invariant).

IRComponent.register('LuaWaveState', {
    -- amplitude_ (vec3) flattened
    amp_x  = { type = 'float', default = 0.0 },
    amp_y  = { type = 'float', default = 0.0 },
    amp_z  = { type = 'float', default = 1.0 },

    -- live state advanced by tick
    angle              = { type = 'float', default = 0.0 },
    angle_inc          = { type = 'float', default = 0.0 },
    current_stage      = { type = 'int32', default = 0 },
    cycle_completed    = { type = 'bool',  default = false },

    -- m_currentValue (vec3) flattened
    out_x = { type = 'float', default = 0.0 },
    out_y = { type = 'float', default = 0.0 },
    out_z = { type = 'float', default = 0.0 },

    -- pause control
    pause_requested  = { type = 'bool',  default = false },
    paused           = { type = 'bool',  default = false },

    period           = { type = 'float', default = 4.0 },
    resume_countdown = { type = 'float', default = 0.0 },

    -- stage 0 (PeriodStage), defaults: 0 .. pi, -1 -> 1, SineEaseInOut
    s0_end_angle    = { type = 'float', default = 3.1415927 },
    s0_end_t        = { type = 'float', default =  1.0 },
    s0_reversed     = { type = 'bool',  default = false },
    s0_start_angle  = { type = 'float', default = 0.0 },
    s0_start_t      = { type = 'float', default = -1.0 },

    -- stage 1, defaults: pi .. 2*pi, 1 -> -1, SineEaseInOut
    s1_end_angle    = { type = 'float', default = 6.2831853 },
    s1_end_t        = { type = 'float', default = -1.0 },
    s1_reversed     = { type = 'bool',  default = false },
    s1_start_angle  = { type = 'float', default = 3.1415927 },
    s1_start_t      = { type = 'float', default =  1.0 },

    tick_count = { type = 'int32', default = 0 },
})

LuaWaveTickSysId = IRSystem.registerSystem({
    name = 'LuaWaveTick',
    components = { 'LuaWaveState' },
    -- The per-component emit shape clears the `isBatchForm_`
    -- FATAL in `validateConcurrencyForAccess`, unlocking PARALLEL_FOR
    -- for codegen'd systems. The wave body only reads/writes its own
    -- row and calls whitelisted intrinsics, so the worker dispatch is
    -- race-free without further opt-in.
    concurrency = IRSystem.Concurrency.PARALLEL_FOR,
    -- Tick body. Mirrors C_PeriodicIdle::tick() for the 2-stage
    -- SineEaseInOut case. The CODEGEN DSL forbids `while` loops, so
    -- the C++ stage-advance loop is unrolled into a single `if`
    -- (correct for the 2-stage case — the loop body can only fire once).
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local s = arch.LuaWaveState:at(i)

            if s.paused then
                -- if (paused_) { if (resumeCountdownSec_ > 0) {...} return; }
                if s.resume_countdown > 0.0 then
                    local new_countdown = s.resume_countdown - 0.016666667
                    local new_paused = true
                    if new_countdown <= 0.0 then
                        new_countdown = 0.0
                        new_paused = false
                    end
                    arch.LuaWaveState:setAt(i, LuaWaveState.new(
                        s.amp_x, s.amp_y, s.amp_z,
                        s.angle, s.angle_inc,
                        s.current_stage, s.cycle_completed,
                        s.out_x, s.out_y, s.out_z,
                        s.pause_requested, new_paused,
                        s.period, new_countdown,
                        s.s0_end_angle, s.s0_end_t, s.s0_reversed,
                        s.s0_start_angle, s.s0_start_t,
                        s.s1_end_angle, s.s1_end_t, s.s1_reversed,
                        s.s1_start_angle, s.s1_start_t,
                        s.tick_count
                    ))
                end
                -- else: paused with countdown == 0 → no writes, mirrors
                -- the C++ early `return;` after the inner-if block.
            else
                -- cycleCompleted_ = false; tickCount_++; angle_ += increment;
                local cycle_completed = false
                local tick_count = s.tick_count + 1
                local angle = s.angle + s.angle_inc
                local current_stage = s.current_stage
                local paused = s.paused
                local pause_requested = s.pause_requested

                -- if (angle_ >= 2*pi) { wrap; idx=0; cycleCompleted=true;
                --                       if (pauseRequested_) {...} }
                if angle >= 6.2831853 then
                    angle = angle - 6.2831853
                    current_stage = 0
                    cycle_completed = true
                    if s.pause_requested then
                        angle = 0.0
                        paused = true
                        pause_requested = false
                    end
                end

                -- C++: while (angle_ >= stages_[idx].endAngle_ && idx < size-1) ++idx;
                -- For 2 stages (size-1 == 1) this is one bump at most:
                if current_stage == 0 and angle >= s.s0_end_angle then
                    current_stage = 1
                end

                -- updateValue(): pick active stage, mapAngleToStageTValue,
                -- apply easing, lerp from startT to endT, multiply by amplitude_.
                local stage_start_angle = s.s0_start_angle
                local stage_end_angle   = s.s0_end_angle
                local stage_start_t     = s.s0_start_t
                local stage_end_t       = s.s0_end_t
                local stage_reversed    = s.s0_reversed
                if current_stage == 1 then
                    stage_start_angle = s.s1_start_angle
                    stage_end_angle   = s.s1_end_angle
                    stage_start_t     = s.s1_start_t
                    stage_end_t       = s.s1_end_t
                    stage_reversed    = s.s1_reversed
                end

                -- mapAngleToStageTValue: mirrors the C++ behaviour where
                -- `clampedAngle` is computed but unused, then relativePosition
                -- is derived from the un-clamped angle and then clamped
                -- into [0, 1].
                local relative = (angle - stage_start_angle) /
                                 (stage_end_angle - stage_start_angle)
                if relative < 0.0 then relative = 0.0 end
                if relative > 1.0 then relative = 1.0 end
                if stage_reversed then relative = 1.0 - relative end

                -- glm::sineEaseInOut<float>(t) := -0.5 * (cos(pi*t) - 1)
                local eased = -0.5 * (math.cos(3.1415927 * relative) - 1.0)

                -- mix(startT, endT, eased) := startT + (endT - startT) * eased
                local mixed = stage_start_t + (stage_end_t - stage_start_t) * eased

                -- amplitude_ * easedValue (vec3 scaled by scalar)
                local out_x = s.amp_x * mixed
                local out_y = s.amp_y * mixed
                local out_z = s.amp_z * mixed

                arch.LuaWaveState:setAt(i, LuaWaveState.new(
                    s.amp_x, s.amp_y, s.amp_z,
                    angle, s.angle_inc,
                    current_stage, cycle_completed,
                    out_x, out_y, out_z,
                    pause_requested, paused,
                    s.period, s.resume_countdown,
                    s.s0_end_angle, s.s0_end_t, s.s0_reversed,
                    s.s0_start_angle, s.s0_start_t,
                    s.s1_end_angle, s.s1_end_t, s.s1_reversed,
                    s.s1_start_angle, s.s1_start_t,
                    tick_count
                ))
            end
        end
    end,
})
