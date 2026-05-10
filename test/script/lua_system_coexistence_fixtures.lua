-- T-108 regression coverage: one .lua file declaring one CODEGEN system
-- (no `mode` field, so it follows the codegen tool's --default-mode=codegen)
-- and one EVAL system (explicit `mode = "eval"`). Exercises the per-system
-- mode override path: at build time the codegen tool emits
-- `createSystem_CoexistAddOneCodegen()` for the CODEGEN system and skips
-- C++ emission for the EVAL system. At runtime, the same .lua file is
-- loaded again — IRComponent.register returns the existing C++-bound handle
-- (no duplicate-registration error), the unmarked IRSystem.registerSystem
-- call no-ops because the LuaScript runtime default matches the build-time
-- default (CODEGEN), and the `mode = "eval"` call registers via the
-- existing T-101 dynamic path.
--
-- Component names use a `Coexist` prefix to keep them distinct from the
-- T-106/T-107 fixtures so all three test binaries can coexist in one
-- IrredenEngineTest run without colliding on Lua name registration.

IRComponent.register('CoexistPos', {
    x = { type = 'float', default = 0 },
})

-- CODEGEN system (mode absent → uses creation default CODEGEN).
-- Adds 1 to pos.x per tick. The codegen tool emits this as a typed
-- `IRSystem::createSystem<C_CoexistPos>` specialisation.
IRSystem.registerSystem({
    name = 'CoexistAddOneCodegen',
    components = { 'CoexistPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local p = arch.CoexistPos:at(i)
            arch.CoexistPos:setAt(i, CoexistPos.new(p.x + 1.0))
        end
    end,
})

-- EVAL system (explicit mode override). Adds 10 to pos.x per tick. The
-- codegen tool skips C++ emission; the runtime LuaScript registers it via
-- the existing IComponentDataLuaTyped / createSystemDynamic path. The
-- captured SystemId is parked in a global so the C++ test side can pluck
-- it for `replaceSystemBody` exercise without relying on lookup-by-name
-- (which SystemManager does not expose).
CoexistEvalSysId = IRSystem.registerSystem({
    name = 'CoexistAddTenEval',
    mode = 'eval',
    components = { 'CoexistPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local p = arch.CoexistPos:at(i)
            arch.CoexistPos:setAt(i, CoexistPos.new(p.x + 10.0))
        end
    end,
})
