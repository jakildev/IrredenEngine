-- response_stress: COLLISION_RESPONSE under load, run with --response-stress
-- in place of main.lua. IREntity.spawnResponseStress() (C++) drops 2000
-- collider-only bodies with per-body gravity onto 40 static boxes and a floor
-- plane; `--auto-profile N` closes after N UPDATE ticks with a
-- RESPONSE_STRESS summary line.
IREntity.spawnResponseStress()

local SystemName = IRSystem.SystemName

-- No C_Velocity3D writer may sit between VELOCITY_3D and COLLISION_RESPONSE:
-- the response recovers each body's sweep start from this tick's velocity.
IRSystem.registerPipeline(IRTime.UPDATE, {
    IRSystem.systemId(SystemName.ACCELERATION_3D),
    IRSystem.systemId(SystemName.VELOCITY_3D),
    IRSystem.systemId(SystemName.PROPAGATE_TRANSFORM),
    IRSystem.systemId(SystemName.COLLISION_RESPONSE),
})

IRSystem.registerPipeline(IRTime.RENDER, {
    IRSystem.systemId(SystemName.TRIXEL_TO_FRAMEBUFFER),
    IRSystem.systemId(SystemName.FRAMEBUFFER_TO_SCREEN),
    IRSystem.systemId(SystemName.GUI_CANVAS_TO_SCREEN),
})
