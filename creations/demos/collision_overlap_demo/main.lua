-- collision_overlap_demo (#1817): prove the Lua-facing overlap callback.
--
-- Two collision-layer pairs, isolated by their collidesWith masks:
--   * pickup overlaps player    -> onOverlapEnter(PICKUP, PLAYER)
--   * projectile overlaps enemy -> onOverlapEnter/Exit(PROJECTILE, ENEMY)
--
-- The named engine layers (IRCollision.Layer.COLLISION_LAYER_*) are music-demo
-- specific, so the demo uses its OWN gameplay layer bits — raw integers the
-- engine accepts unchanged. This is the arcade case the issue targets.
local PLAYER = 16     -- 1 << 4
local ENEMY = 32      -- 1 << 5
local PROJECTILE = 64 -- 1 << 6
local PICKUP = 128    -- 1 << 7

-- spawnCollidable(localTransform, collider, layer, contact, velocity).
-- createEntity auto-adds C_WorldTransform, so the entity joins the collision
-- archetype; PROPAGATE_TRANSFORM keeps world position in sync with velocity.
local function spawn(x, y, z, layer, collidesWith, vx, vy, vz)
    return IREntity.spawnCollidable(
        C_LocalTransform.new(vec3.new(x, y, z)),
        C_ColliderIso3DAABB.new(0.5, 0.5, 0.5),
        C_CollisionLayer.new(layer, collidesWith, true),
        C_ContactEvent.new(),
        C_Velocity3D.new(vx, vy, vz)
    )
end

-- pickup-vs-player: both static, spawned overlapping -> ENTER on frame 1.
spawn(0.0, 0.0, 0.0, PLAYER, PICKUP, 0.0, 0.0, 0.0)
spawn(0.0, 0.0, 0.0, PICKUP, PLAYER, 0.0, 0.0, 0.0)

-- projectile-vs-enemy: spawned overlapping (ENTER frame 1); the projectile
-- drifts +x and separates within a few frames (EXIT). 8 blocks/s clears the
-- 1-block overlap window (0.5 + 0.5 half-extents) in well under a second.
spawn(20.0, 0.0, 0.0, ENEMY, PROJECTILE, 0.0, 0.0, 0.0)
spawn(20.0, 0.0, 0.0, PROJECTILE, ENEMY, 8.0, 0.0, 0.0)

-- Overlap handlers. The entity on the FIRST layer arg is the callback's first
-- arg (D4), so a creation never re-checks layers in Lua.
IRCollision.onOverlapEnter(PICKUP, PLAYER, function(pickup, player)
    print("OVERLAP_DEMO PICKUP pickup=" .. pickup .. " player=" .. player)
end)
IRCollision.onOverlapEnter(PROJECTILE, ENEMY, function(projectile, enemy)
    print("OVERLAP_DEMO HIT projectile=" .. projectile .. " enemy=" .. enemy)
end)
IRCollision.onOverlapExit(PROJECTILE, ENEMY, function(projectile, enemy)
    print("OVERLAP_DEMO MISS projectile=" .. projectile .. " enemy=" .. enemy)
end)

local SystemName = IRSystem.SystemName

-- VELOCITY -> PROPAGATE (local->world) -> CLEAR (reset C_ContactEvent) ->
-- NOTE_PLATFORM (detect + emit pairs) -> DISPATCH (route enter/exit to Lua,
-- then consume the batch). DISPATCH must run after NOTE_PLATFORM.
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.VELOCITY_3D),
    IRSystem.systemId(SystemName.PROPAGATE_TRANSFORM),
    IRSystem.systemId(SystemName.COLLISION_EVENT_CLEAR),
    IRSystem.systemId(SystemName.COLLISION_NOTE_PLATFORM),
    IRSystem.systemId(SystemName.DISPATCH_LUA_OVERLAP),
})

-- Minimal render path so the engine presents / a headless --auto-screenshot
-- run captures cleanly. The demo's signal is the handler prints, not pixels.
IRSystem.registerPipeline(IRTime.RENDER, {
    IRSystem.systemId(SystemName.TRIXEL_TO_FRAMEBUFFER),
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
})
