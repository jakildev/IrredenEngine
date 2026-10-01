-- Sample creation: entire initSystems lives here. No C++ initSystems.
-- Demonstrates IRSystem.registerPipeline + IRSystem.systemId mixing prefab
-- systems with one Lua-defined system in the UPDATE pipeline. The full
-- modifier-resolver chain is in the same UPDATE list so any consumer
-- system can read C_ResolvedFields downstream of it. The unit test at
-- test/script/lua_pipeline_test.cpp covers IRModifier.add's resolved-
-- value composition against a Lua-defined component; here we just prove
-- the wiring runs end-to-end through the game loop.
--
-- Also exercises the shared render-glue bindings: the
-- IRRender.setSun*/setSky* lighting setters and a RENDER-phase Lua system
-- that draws a HUD via IRGui.drawDisc / IRGui.drawLine onto the gui canvas.

local SystemName = IRSystem.SystemName

local tickCounterSysId = IRSystem.registerSystem({
    name = "TickCounterLua",
    -- The procedural voxel entity exercises this Lua-defined system alongside
    -- prefab SystemIds in the same pipeline.
    components = { IRComponent.C_LocalTransform },
    tick = function(arch)
        -- Reading the matching archetype proves the Lua tick executes.
        local _ = arch.length
    end,
})

IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.PROPAGATE_TRANSFORM),
    IRSystem.systemId(SystemName.UPDATE_VOXEL_SET_CHILDREN),
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

-- Render-glue setters. The render manager exists by the time
-- main.lua runs, so these drive lighting from Lua without a per-creation
-- pass-through. There is no lit geometry in this demo, so the effect isn't
-- visible — the call simply exercises the shared binding at runtime.
IRRender.setSunDirection(0.4, 0.4, -1.0)
IRRender.setSunIntensity(1.0)
IRRender.setSkyColor(0.25, 0.3, 0.45)

local recipe = dofile("scripts/procedural_voxel_recipe.lua")
local voxelSet = C_VoxelSetNew.new(
    ivec3.new(25, 25, 18),
    Color.new(0, 0, 0, 0),
    IRComponent.EntityAnchor.CENTER
)
local edits = recipe.build({ radius = 8, height = 14 })
voxelSet:batch(function(set)
    for _, voxel in ipairs(edits) do
        set:setVoxel(voxel.x, voxel.y, voxel.z, Color.new(voxel.r, voxel.g, voxel.b, 255))
    end
end)
IREntity.createVoxelEntity(C_LocalTransform.new(vec3.new(0.0, 0.0, 0.0)), voxelSet)

-- GUI shape draw. The shape-draw primitives are immediate-mode
-- onto the engine-default "gui" trixel canvas, so they must run every frame
-- from a RENDER-phase system. One marker entity (singleton) puts the HUD
-- system's archetype in scope so its tick fires once per frame.
local C_HudMarker = IRComponent.register("HudMarker", { dummy = 0 })
IREntity.singleton(C_HudMarker)

local hudDrawSysId = IRSystem.registerSystem({
    name = "HudDraw",
    components = { C_HudMarker },
    tick = function(arch)
        IRGui.drawDisc(40, 30, 18, { 235, 90, 70 })       -- filled disc
        IRGui.drawLine(8, 56, 96, 56, { 90, 200, 255 })   -- horizontal line
        IRGui.drawLine(40, 8, 40, 56, { 120, 235, 140 })  -- vertical line

    end,
})

IRSystem.registerPipeline(IRTime.RENDER, {
    IRSystem.systemId(SystemName.VOXEL_TO_TRIXEL_STAGE_1),
    hudDrawSysId,
    IRSystem.systemId(SystemName.TRIXEL_TO_FRAMEBUFFER),
    IRSystem.systemId(SystemName.DEBUG_OVERLAY),
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
})
