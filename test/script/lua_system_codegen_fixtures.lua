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

-- #1368: packed vec3 / ivec3 fields consumed inside a CODEGEN tick.
IRComponent.register('CodegenSysBody', {
    pos = { type = 'vec3', default = { 0, 0, 0 } },
    cell = { type = 'ivec3', default = { 0, 0, 0 } },
})

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

-- #1353 row-alias safety: bind the row, write field x through the column,
-- then read x AGAIN (after the write) to set y. The binding must stay a
-- by-value copy — an alias would observe the just-written x and emit y == 99
-- instead of the original x. Pins the analysis that blocks aliasing when a
-- read is sequenced after a same-column write.
IRSystem.registerSystem({
    name = 'CodegenReadAfterWrite',
    components = { 'CodegenSysPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local r = arch.CodegenSysPos:at(i)
            arch.CodegenSysPos:setField(i, 'x', 99.0)
            arch.CodegenSysPos:setField(i, 'y', r.x)
        end
    end,
})

-- #1368: packed vec3 / ivec3 fields in a CODEGEN tick. Reads a packed field
-- via getField (typed VEC3 / IVEC3), accesses its components (`.x/.y/.z`),
-- builds a fresh value with the `vec3.new` / `ivec3.new` built-in constructors,
-- and writes it back via setField — the full DSL round-trip for packed fields.
IRSystem.registerSystem({
    name = 'CodegenVecStep',
    components = { 'CodegenSysBody' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local p = arch.CodegenSysBody:getField(i, 'pos')
            arch.CodegenSysBody:setField(i, 'pos', vec3.new(p.x + 1.0, p.y + 2.0, p.z + 3.0))
            local c = arch.CodegenSysBody:getField(i, 'cell')
            arch.CodegenSysBody:setField(i, 'cell', ivec3.new(c.x + 1, c.y, c.z))
        end
    end,
})

-- #1353 FOR_NUMERIC back-edge: bind the row before a nested for_numeric loop
-- that writes the same component. Across the loop back-edge, iteration j+1
-- reads `a.x` after iteration j already wrote it — the binding must stay a
-- by-value copy. With initial x=1.0 and 3 iterations (j=0,1,2), copy
-- semantics produce x=2.0 each write (reads unchanged 1.0); an alias would
-- accumulate: 1→2→3→4.
IRSystem.registerSystem({
    name = 'CodegenForNumericBackEdge',
    components = { 'CodegenSysPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local a = arch.CodegenSysPos:at(i)
            for j = 0, 2 do
                local tmp = a.x
                arch.CodegenSysPos:setField(i, 'x', tmp + 1.0)
            end
        end
    end,
})

-- #1616: whitelisted side-effecting engine binding in a CODEGEN tick body.
-- `IRRender.setSunIntensity(x)` is a void render-glue setter — allowed as a
-- bare statement (NOT inside an expression) and lowered to the C++ free
-- function `IRRender::setSunIntensity(...)`. The generated header compiling +
-- linking against the real symbol is the proof the bare-call lowering is valid
-- C++; the sibling test registers the system but does not execute its tick
-- against a matching entity (the render setter asserts on an absent
-- RenderManager in the headless test harness, engine/render/src/ir_render.cpp).
IRSystem.registerSystem({
    name = 'CodegenRenderGlue',
    components = { 'CodegenSysPos' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local pos = arch.CodegenSysPos:at(i)
            IRRender.setSunIntensity(pos.x)
        end
    end,
})
