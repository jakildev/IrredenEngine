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

fogCapSelftestDone()
print("LUA-FOG-CAP requested=9 PASS")
