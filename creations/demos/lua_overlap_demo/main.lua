-- #1817 sample creation: Lua-facing collision/trigger overlap events.
--
-- The C++ side (main_lua.cpp) spawns two overlapping arcade pairs and hands
-- this script the layer bits as globals. Here we register one overlap handler
-- per pair via IRCollision.onOverlapEnter, compose the collision UPDATE
-- pipeline (producer -> dispatch -> clear), and draw a HUD whose two discs
-- turn green once their handler has fired. The --auto-screenshot frame is a
-- direct visual proof that both overlap callbacks ran from Lua.

local SystemName = IRSystem.SystemName

-- Latched once each handler fires; drives the HUD color + a console log.
local projEnemyHit = false
local pickupPlayerHit = false

IRCollision.onOverlapEnter(PROJECTILE, ENEMY, function(projectile, enemy)
    if not projEnemyHit then
        print(string.format("overlap: projectile %d vs enemy %d", projectile, enemy))
    end
    projEnemyHit = true
end)

IRCollision.onOverlapEnter(PICKUP, PLAYER, function(pickup, player)
    if not pickupPlayerHit then
        print(string.format("overlap: pickup %d vs player %d", pickup, player))
    end
    pickupPlayerHit = true
end)

-- Collision UPDATE pipeline: NOTE_PLATFORM emits the contact-pair buffer,
-- DISPATCH_LUA_OVERLAP diffs it and fires the Lua handlers, EVENT_CLEAR resets
-- the per-entity C_ContactEvent flags.
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.COLLISION_NOTE_PLATFORM),
    IRSystem.systemId(SystemName.COLLISION_DISPATCH_LUA_OVERLAP),
    IRSystem.systemId(SystemName.COLLISION_EVENT_CLEAR),
})

-- HUD: one marker singleton puts the draw system's archetype in scope so its
-- tick fires once per frame (immediate-mode draw onto the "gui" canvas).
local C_HudMarker = IRComponent.register("HudMarker", { dummy = 0 })
IREntity.singleton(C_HudMarker)

local GREEN = { 90, 220, 110 }
local RED = { 220, 80, 80 }

local hudDrawSysId = IRSystem.registerSystem({
    name = "OverlapHud",
    components = { C_HudMarker },
    tick = function(arch)
        -- Left disc = projectile/enemy handler, right disc = pickup/player.
        IRGui.drawDisc(36, 30, 16, projEnemyHit and GREEN or RED)
        IRGui.drawDisc(84, 30, 16, pickupPlayerHit and GREEN or RED)
    end,
})

IRSystem.registerPipeline(IRTime.RENDER, {
    hudDrawSysId,
    IRSystem.systemId(SystemName.TRIXEL_TO_FRAMEBUFFER),
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
})
