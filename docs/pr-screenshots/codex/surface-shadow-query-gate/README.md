# Skip sun queries with no directional contribution

The shared analytical/attached surface-lighting helper now skips visibility
queries when shadows are disabled, Lambert is exactly zero, sun intensity is
exactly zero, or ambient is exactly one. Each condition removes the visibility
term from `(ambient + (1-ambient)*Lambert*visibility)*intensity`. The original
composition order remains unchanged; local light, sky, AO and palette are still
computed. No epsilon, filtering, face geometry or diagnostic query is changed.

## Native verification

Metal Debug on Apple M4 Max, macOS 26.5.2, battery power, 2560×1440.
All runs enable `IR_PERAXIS_SURFACE_LIGHTING=1`. The four identity-frame views,
five mixed-scene views (including cardinal), and five zero-sun HDR sky views are
RGB-identical to the parent evidence. See `comparison.json` and each capture
manifest for the parent filename, exact command and build/shader fingerprints.
The frame retains the parent independent oracle result: 208,344 sun-facing
interior pixels with zero errors. This is parity with that bounded acceptance,
not a new claim of full-scene correctness or native OpenGL validation.

![Unscaled sharp-shadow frame detail](frame-detail.png)

## Profile result

Three fresh baseline runs precede three candidate runs, using the same binary
and scene at yaw 73.125°, zoom 1, subdivision 1, 363 frames per run. The staged
shader fingerprint changes; raw profiles and manifests are retained under
`perf/`. Each configuration has 1,533 peak overflow entries and zero drops,
valid GPU timestamps, fixed yaw and fixed zoom. Every retained performance
capture pair is also RGB-identical (`perf-comparison.json`).

| Measurement | Before mean (range), ms | After mean (range), ms |
|---|---:|---:|
| GPU frame envelope | 10.007 (9.890–10.185) | 9.815 (9.784–9.855) |
| Per-axis scatter scope | 5.308 (5.295–5.315) | 5.198 (5.150–5.230) |
| Steady frame average | 13.603 (13.530–13.660) | 13.570 (13.530–13.610) |

This is a modest measured reduction, not a large throughput gain. Sequential
runs on battery are susceptible to host variation. The wider-view fragment
query cost remains large, so attached presentation lighting stays default-off.
Do not sum overlapping Metal scopes or interpret the envelope as GPU busy time.
This small scene does not establish population or subdivision scaling.

## Deterministic controls

Each backend executes 20,736 shape cases and 6,912 direct-helper cases. Expected
colors use independent composition, while expected query counts use positive
directional energy evaluated in double precision. Seven negative controls fail
specifically on query counts: unconditional work, each zero-contribution case,
and thresholds swallowing tiny positive intensity, grazing light or ambient
immediately below one. Both backend expressions pass; native Metal captures
cover their presentation, and OpenGL runtime remains unverified.

## Next

Profile candidate visits and conservative-raster overdraw. Investigate a
conservative projected-face bounds rejection before exact intersection work;
retain the exact finite-face test and prove that every accepted boundary hit
survives before adoption. Incomplete-index accuracy, cardinal transitions, fog
presentation and dense overflow still gate default adoption.
