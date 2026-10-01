-- One --los-query-bench round's Lua arms over the demo's eye and targets:
-- K IRFog.lineOfSight calls, then one capture plus one batched
-- IRFog.lineOfSightCaptured over the same K targets.
local eye = fogLosBenchEye()
local targets = fogLosBenchTargets()
local count = #targets / 3

local perCall = {}
local start = fogBenchMicros()
for i = 0, count - 1 do
    perCall[i + 1] = IRFog.lineOfSight(
        eye[1], eye[2], eye[3],
        targets[3 * i + 1], targets[3 * i + 2], targets[3 * i + 3]
    )
end
local perCallMicros = fogBenchMicros() - start

start = fogBenchMicros()
IRFog.captureLineOfSight()
local captured = IRFog.lineOfSightCaptured(eye[1], eye[2], eye[3], targets)
local capturedMicros = fogBenchMicros() - start

fogLosBenchReport(perCallMicros, capturedMicros, perCall, captured)
