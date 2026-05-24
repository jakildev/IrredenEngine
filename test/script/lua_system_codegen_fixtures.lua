-- T-107 regression coverage: end-to-end fixture exercised by the codegen
-- tool. Components below are codegen'd as `C_CodegenSys*` structs (T-106
-- path); systems below are codegen'd as `IRScript::CodegenRegistry::
-- createSystem_*()` functions whose tick body translates the Lua DSL into
-- typed C++. The sibling test file `lua_system_codegen_test.cpp` drives
-- both paths and asserts behavioral parity with the EVAL register surface.
--
-- The component names use a `CodegenSys` prefix to keep them distinct from
-- T-106's component-only fixture (`CodegenHp` / `CodegenVel` / ...) so both
-- fixtures can coexist in the same test binary without colliding on Lua
-- name registration.

IRComponent.register('CodegenSysPos', {
    x = { type = 'float', default = 0 },
    y = { type = 'float', default = 0 },
})

IRComponent.register('CodegenSysVel', {
    x = { type = 'float', default = 0 },
    y = { type = 'float', default = 0 },
})

IRComponent.register('CodegenSysHp', {
    current = 100,
    max = 100,
})

IRComponent.register('CodegenSysSkip', { dummy = 0 })

-- Canonical loop + column at/setAt with constructed value: the smallest
-- "real" CODEGEN system. Translates to a `IRSystem::createSystem<...>` with
-- a per-archetype tick body that walks the column vectors directly.
IRSystem.registerSystem({
    name = 'CodegenMove',
    components = { 'CodegenSysPos', 'CodegenSysVel' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            local vel = arch.CodegenSysVel:at(i)
            arch.CodegenSysPos:setAt(i,
                CodegenSysPos.new(pos.x + vel.x, pos.y + vel.y))
        end
    end,
})

-- Math intrinsic exercised through the whitelist: `math.sin` maps to
-- `IRMath::sin`. Asserts the codegen tool routes math.* calls through the
-- IRMath wrapper (per the engine's no-`std::sin`-outside-engine/math/ rule).
IRSystem.registerSystem({
    name = 'CodegenWobble',
    components = { 'CodegenSysPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            arch.CodegenSysPos:setAt(i,
                CodegenSysPos.new(pos.x + math.sin(pos.y), pos.y))
        end
    end,
})

-- Branches: `if`/`else` translates straight to the C++ equivalent. Used to
-- check that the parser tracks block scope (the `local pos` inside the
-- branch must not leak out, which the symbols-snapshot logic in the
-- emitter enforces).
IRSystem.registerSystem({
    name = 'CodegenClampPositive',
    components = { 'CodegenSysPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            if pos.x < 0.0 then
                arch.CodegenSysPos:setAt(i, CodegenSysPos.new(0.0, pos.y))
            end
        end
    end,
})

-- Excludes filter: skip archetypes that include `CodegenSysSkip`. The
-- emitter materialises this as `IRSystem::Exclude<C_CodegenSysSkip>` in
-- the createSystem<> template parameter list.
IRSystem.registerSystem({
    name = 'CodegenAddOne',
    components = { 'CodegenSysPos' },
    excludes = { 'CodegenSysSkip' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            arch.CodegenSysPos:setAt(i, CodegenSysPos.new(pos.x + 1.0, pos.y))
        end
    end,
})

-- getField / setField column ops on a Lua-defined component. Field name
-- must be a string literal — `setField(i, fieldName, ...)` with a variable
-- name would be a codegen-time error.
IRSystem.registerSystem({
    name = 'CodegenDamage',
    components = { 'CodegenSysHp' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local cur = arch.CodegenSysHp:getField(i, 'current')
            arch.CodegenSysHp:setField(i, 'current', cur - 10)
        end
    end,
})

-- PARALLEL_FOR: verifies registration succeeds and the body runs on every row.
IRSystem.registerSystem({
    name = 'CodegenParallelInc',
    components = { 'CodegenSysPos' },
    concurrency = IRSystem.Concurrency.PARALLEL_FOR,
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            arch.CodegenSysPos:setAt(i,
                CodegenSysPos.new(pos.x + 1.0, pos.y + 2.0))
        end
    end,
})
