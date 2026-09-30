IRFog.clearVisions()
for i = 1, 9 do
    IRFog.addVision(i * 20, 0, 2)
end

local entity = fogSelftestEntity()
assert(IRFog.getEntityReveal(entity) == 1)
IRFog.setEntityGoverned(entity)
assert(IRFog.getEntityReveal(entity) == 0)
IRFog.setEntityGoverned(entity, false)
assert(IRFog.getEntityReveal(entity) == 1)

-- LuaEntity is engine-registered for every creation, including this one,
-- which binds no Lua-driven ECS surface. The collection runs first because a
-- stripped usertype only shows once the GC has finalized a replaced registration.
collectgarbage("collect")
local handle = fogSelftestEntityHandle()
assert(handle.entity == entity)
assert(IRFog.getEntityReveal(handle) == 1)
print("LUA-ENTITY-HANDLE entity=" .. tostring(handle.entity) .. " PASS")

fogCapSelftestDone()
print("LUA-FOG-CAP requested=9 PASS")
