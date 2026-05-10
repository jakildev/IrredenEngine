-- Lua-driven ECS perf-parity demo. Schema + wave system live here as the
-- single source of truth: cmake/lua_codegen reads this file at build time
-- and emits a C++ struct + typed System<...> specialisation for the wave
-- tick. At runtime the same file is loaded again — IRComponent.register is
-- idempotent against the codegen pre-bind, and IRSystem.registerSystem
-- no-ops the unmarked call when the runtime EcsDefaultMode is CODEGEN
-- (set by C++ from kDefaultEcsMode). Under -DIR_LUA_ECS_DEFAULT_MODE=EVAL
-- the unmarked call registers via the dynamic LuaJIT path instead.
--
-- Field order in LuaWaveState.new(...) follows the codegen tool's
-- alphabetical sort: amp, out, period, phase, time. The tick body
-- exercises the canonical hot loop the parity gate measures: read row,
-- advance time by a fixed-step delta, evaluate sin, write row back.

IRComponent.register('LuaWaveState', {
    amp    = { type = 'float', default = 1.0 },
    out    = { type = 'float', default = 0.0 },
    period = { type = 'float', default = 4.0 },
    phase  = { type = 'float', default = 0.0 },
    time   = { type = 'float', default = 0.0 },
})

LuaWaveTickSysId = IRSystem.registerSystem({
    name = 'LuaWaveTick',
    components = { 'LuaWaveState' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local s = arch.LuaWaveState:at(i)
            local new_time = s.time + 0.016666667 -- 1/60 s fixed step
            local angle = (new_time / s.period) * 6.2831853 + s.phase -- 2*pi (math.pi unsupported by codegen DSL)
            local out_value = s.amp * math.sin(angle)
            arch.LuaWaveState:setAt(
                i,
                LuaWaveState.new(s.amp, out_value, s.period, s.phase, new_time)
            )
        end
    end,
})
