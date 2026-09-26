IRFog.clear()
IRFog.setCell(3, 4, IRFog.State.EXPLORED)
assert(IRFog.getCell(3, 4) == IRFog.State.EXPLORED)
local cellOk = pcall(IRFog.setCell, 3, 4, 7)
assert(not cellOk)
assert(IRFog.getCell(3, 4) == IRFog.State.EXPLORED)
IRFog.revealRadius(0, 0, 2)
assert(IRFog.getCell(0, 0) == IRFog.State.VISIBLE)
IRFog.clear()
assert(IRFog.getCell(0, 0) == IRFog.State.UNEXPLORED)

IRFog.clearVisions()
assert(IRFog.setVision(-10, 0, 4, 0, 3, 0.5, -1, 1) == 0)
assert(IRFog.addVision(10, 0, 4, 0, 3, 0.5, -1, 1) == 1)
assert(IRFog.evalReveal(-10, 0, 3) == 1)
assert(IRFog.evalReveal(10, 0, 3) == 1)
assert(IRFog.evalReveal(0, 0, 3) == 0)
assert(IRFog.lineOfSight(-10, 0, 3, 10, 0, 3))
-- The probe voxel at (0, 0, 4) is ungoverned, so it occludes a ray that passes
-- its column from an eye below its top; a ray beside it stays clear.
assert(not IRFog.lineOfSight(-10, 0, 10, 10, 0, 3))
assert(IRFog.lineOfSight(-10, 0, 10, 10, 6, 3))

local before = IRFog.evalReveal(-10, 0, 3)
local ok = pcall(IRFog.addVision, 0, 0, "bad radius")
assert(not ok)
assert(IRFog.evalReveal(-10, 0, 3) == before)

-- Gate the second source on the smooth line-of-sight gate; an unregistered
-- slot raises instead of reaching the C++ assert.
IRFog.setVisionLineOfSight(1, 1.5, 0.75)
local losOk = pcall(IRFog.setVisionLineOfSight, 2, 1.5)
assert(not losOk)

fogSetupSelftestDone()
print("LUA-FOG-SETUP sources=2 centers=-10,0;10,0 PASS")
