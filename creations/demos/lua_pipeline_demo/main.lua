-- T-102 sample creation: entire initSystems lives here. No C++ initSystems.
-- Demonstrates IRSystem.registerPipeline + IRSystem.systemId mixing prefab
-- systems with one Lua-defined system in the UPDATE pipeline. The full
-- modifier-resolver chain is in the same UPDATE list so any consumer
-- system can read C_ResolvedFields downstream of it. The unit test at
-- test/script/lua_pipeline_test.cpp covers IRModifier.add's resolved-
-- value composition against a Lua-defined component; here we just prove
-- the wiring runs end-to-end through the game loop.

local SystemName = IRSystem.SystemName

local tickCounterSysId = IRSystem.registerSystem({
    name = "TickCounterLua",
    -- No entity is created in this demo so no archetype matches
    -- C_LocalTransform — the per-archetype tick body never fires. The
    -- point of including this Lua system is to verify pipeline composition
    -- mixes prefab and Lua-defined SystemIds in the same list without
    -- crashing.
    components = { IRComponent.C_LocalTransform },
    tick = function(arch)
        -- Body stays trivial; the demo's success signal is the engine
        -- reaching the game loop with a Lua-driven pipeline + exiting
        -- cleanly under --auto-screenshot.
        local _ = arch.length
    end,
})

IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.PROPAGATE_TRANSFORM),
    IRSystem.systemId(SystemName.LIFETIME),
    IRSystem.systemId(SystemName.MODIFIER_DECAY),
    IRSystem.systemId(SystemName.GLOBAL_MODIFIER_DECAY),
    IRSystem.systemId(SystemName.LAMBDA_MODIFIER_DECAY),
    IRSystem.systemId(SystemName.MODIFIER_RESOLVE_GLOBAL),
    IRSystem.systemId(SystemName.MODIFIER_RESOLVE_EXEMPT),
    IRSystem.systemId(SystemName.MODIFIER_RESOLVE_LAMBDA),
    tickCounterSysId,
})

IRSystem.registerPipeline(IRTime.INPUT, {
    IRSystem.systemId(SystemName.INPUT_KEY_MOUSE),
})

IRSystem.registerPipeline(IRTime.RENDER, {
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
})
