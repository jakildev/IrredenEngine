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
IRFog.setCell(0, 0, IRFog.State.VISIBLE)
assert(IRFog.evalReveal(0, 0, 3) == 1)
IRFog.setCell(0, 0, IRFog.State.EXPLORED)
assert(IRFog.evalReveal(0, 0, 3) == 0)
IRFog.clear()
assert(IRFog.lineOfSight(-10, 0, 3, 10, 0, 3))
-- The probe voxel at (0, 0, 4) is ungoverned, so it occludes a ray that passes
-- its column from an eye below its top; a ray beside it stays clear.
assert(not IRFog.lineOfSight(-10, 0, 10, 10, 0, 3))
assert(IRFog.lineOfSight(-10, 0, 10, 10, 6, 3))
-- A captured view answers the same three queries without a rebuild each.
IRFog.captureLineOfSight()
assert(IRFog.lineOfSightCaptured(-10, 0, 3, {10, 0, 3})[1])
assert(not IRFog.lineOfSightCaptured(-10, 0, 10, {10, 0, 3})[1])
assert(IRFog.lineOfSightCaptured(-10, 0, 10, {10, 6, 3})[1])

local before = IRFog.evalReveal(-10, 0, 3)
local ok = pcall(IRFog.addVision, 0, 0, "bad radius")
assert(not ok)
assert(IRFog.evalReveal(-10, 0, 3) == before)

-- Gate the second source by line of sight with a softness band; an
-- unregistered slot raises instead of reaching the C++ assert.
IRFog.setVisionLineOfSight(1, 1.5, 0.75)
local losOk = pcall(IRFog.setVisionLineOfSight, 2, 1.5)
assert(not losOk)

-- Cap the first source 2.5 above its observer with a 1.5 fade: the plane
-- stays visible, the band's end is hidden, and the second source keeps no
-- ceiling. Invalid calls raise named errors and change nothing.
IRFog.setVisionCeiling(0, 2.5, 1.5)
local ceilingHeight, fadeHeight = IRFog.getVisionCeiling(0)
assert(ceilingHeight == 2.5 and fadeHeight == 1.5)
assert(select(1, IRFog.getVisionCeiling(1)) == -1)
assert(IRFog.evalReveal(-10, 0, 0.5) == 1)
assert(IRFog.evalReveal(-10, 0, -1) == 0)
assert(not pcall(IRFog.setVisionCeiling, 2, 1))
assert(not pcall(IRFog.setVisionCeiling, 0, 1, -1))
assert(select(1, IRFog.getVisionCeiling(0)) == 2.5)

-- The canvas treatment round-trips, clears explicitly, and rejects a
-- density or tone outside [0, 1].
IRFog.setRevealSurfaceTreatment(0.5, 0.6)
local on, density, tone = IRFog.getRevealSurfaceTreatment()
assert(on and density == 0.5 and math.abs(tone - 0.6) < 1e-6)
IRFog.clearRevealSurfaceTreatment()
assert(not select(1, IRFog.getRevealSurfaceTreatment()))
assert(not pcall(IRFog.setRevealSurfaceTreatment, 2))
assert(not pcall(IRFog.setRevealSurfaceTreatment, 0.5, -1))
assert(not select(1, IRFog.getRevealSurfaceTreatment()))
IRFog.setRevealSurfaceTreatment(0.25)

fogSetupSelftestDone()
print("LUA-FOG-SETUP sources=2 centers=-10,0;10,0 ceiling=2.5,1.5 treatment=0.25 PASS")
