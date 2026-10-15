# SDF/voxel comparison controls — native smoke, not a speedup verdict

The fixed-pose adapter and matrix runner support the shared-voxel authoring
decision. These records validate the measurement path. They do not establish
that baked occupancy is faster than direct SDF.

## Successful smoke

`smoke/` retains eight single-run macOS/Metal Debug reports: direct/baked ×
yaw 0/45 × stage timing on/off, at zoom 4, 300 frames, with voxel blockers and
AO disabled. Each run was fresh, clean and `QUIET=guarded`, with the requested
pose/zoom, no overflow drops and a GPU measurement. Each report excludes its
first 75 frames from the steady series. The full command and host conditions
are in its manifest. This host was on battery power.

The adapter was tightened during these smoke runs. `retrospective-check.json`
rechecks every saved report against the final pose, frame-series, overflow and
timing-mode validators. It is explicitly a retrospective check, not a claim
that the earliest process executed checks added later. Logs/manifests normalize
local paths; exported text strips trailing blank lines. The raw numeric reports
are retained. The off-preset comment was subsequently corrected without changing
its settings; the matrix attempts fingerprint the updated staged text.

## Rejected repeat matrix

`sdf_controls.py` plans 16 cases (representation × yaw × shadows × timing),
three rounds, forward/reverse/forward order. Two native attempts stopped on
`QUIET=breach`, exit 76 / `RESULT=CONTAMINATED`, when another session became
active during measurement. The engine itself completed cleanly. The failing
reports are retained under `rejected/v2` and `rejected/v3`, with their original
planned cases. Neither attempt produced a complete matrix summary; partial
case timings must not be promoted into a representation comparison.

A preceding sandboxed launch could not initialize macOS display services and
timed out before producing a fresh report. It was rerun with native GUI access;
it contributes no timing evidence.

## Correctness and remaining work

- All 102 tests under `scripts/perf` pass, including missing/incorrect pose,
  frame count, zoom excursions, overflow, timing-mode, stale report, config
  drift and unguarded-window controls. The stage-on control rejects a report
  with only a GPU frame envelope; stage-off rejects sampled stage rows.
- IRShapeDebug builds natively. A separate six-shot baked fixture capture
  exited cleanly and every image is byte-identical to the parent capture.
  [Images and hashes](../../pr-screenshots/codex/sdf-profile-controls/README.md).
- The profiling review caught and corrected the CPU-control description:
  off disables easy_profiler and GPU stage timestamps, while auto-profile
  retains frame/system timers and the Debug CPU histogram. CPU profiler
  enablement and ORIGIN pivot mode remain configuration/source evidence.

Finish the alternating matrix on a quiet host, then repeat in Release and on
Windows/OpenGL. Population, overlap, subdivisions, startup baking, resident
memory and authored rotation/attachments need separate coverage. The four
subjects and shared floor are not a large-entity performance workload. The
known intermediate-yaw voxel floor-shadow gap remains open; profiling controls
do not fix or conceal it.
