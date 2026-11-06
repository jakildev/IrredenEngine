# SDF occupancy memory qualification — Windows/OpenGL Release

This slice repeats the [AC Metal memory slice](../ac-release-memory/README.md)
on the native Windows/OpenGL ship host. It records the same eleven density,
16-copy population and root/parent motion cases, and it does not select a
representation default. The host is a Windows 10 desktop on AC power (the
`GetSystemPowerStatus` witness reports `AC Power`, no battery), with 32 logical
CPUs and an NVIDIA OpenGL device that exposes `GL_NVX_gpu_memory_info`
(8 GiB dedicated). Every run used the same Release `IRShapeDebug` binary,
shader tree and runtime scripts: binary SHA-256
`6d19cab1be19487f8a240af6c7ac9b37751dc9fed42f285aa8cecfec67ec6ff4`, built from
`5d93af97c` plus this change.

Two guarded matrices ran with three rounds each, forward/reverse/forward, 300
measured frames, zoom 4, yaw 45°, voxel blockers on and AO off:

- **timing-off** (`configs/perf/sdf-off.lua`): the instrumentation-off control.
  It supplies the whole-frame, bake and memory numbers below.
- **timing-on** (`configs/perf/sdf-on.lua`): the same cases with GPU stage
  timestamps. It supplies the GPU stage costs.

All 66 runs exited `RESULT=CLEAN` under `QUIET=guarded`, with fresh reports,
unchanged configuration fingerprints, both `SDF-OCCUPANCY` and
`SDF-RESIDENCY` witnesses, and zero overflow drops. `runs.json` holds the
per-run witnesses and selected stage means, without local paths. The
`timing-*-summary.md` files are the runner's own summaries.

## OpenGL has no whole-frame GPU timer

The OpenGL `RenderDevice` does not implement the full-frame GPU envelope that
Metal reads from its command buffers. Timing-off reports therefore carry
`Coverage: supported=0` and no GPU measurement at all. The timing-off matrix
ran with `--allow-unsupported-gpu-frame`, which accepts such a run only when
its report witnesses the unsupported timer, and records it as `gpu_measured:
false`. GPU cost on OpenGL comes from the timing-on stage rows. Their
instrumentation adds 0.2–0.9 ms to the steady frame relative to the
timing-off control. The `Retained mean` column in the timing-off summary is
0.0 because cull statistics are only read back when stage timing is on; it is
not a geometry count.

## Bake and memory (timing-off, mean of 3)

Process columns are working-set deltas from a sample taken immediately before
the fixture scene is created. `RSS peak` is the PSAPI peak working set. Asset
and scratch sizes are exact baker payload accounting. The NVX column is
device-wide dedicated capacity minus current availability, across every
process on the device. It is not this process's residency.

| Case | Bake µs | Asset KiB | Scratch KiB | RSS final MiB | RSS peak MiB | NVX device used MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Density baked 1 | 23 | 62.4 | 5.2 | 28.00 | 29.24 | 1851 |
| Density baked 2 | 116 | 434.1 | 36.2 | 29.26 | 30.01 | 1853 |
| Density direct 1 | 0 | 0 | 0 | 32.46 | 33.48 | 1860 |
| Motion baked, parented | 23 | 62.4 | 5.2 | 28.51 | 29.61 | 1876 |
| Motion baked, root | 28 | 62.4 | 5.2 | 29.32 | 30.71 | 1865 |
| Motion direct, parented | 0 | 0 | 0 | 32.89 | 33.28 | 1876 |
| Motion direct, root | 0 | 0 | 0 | 32.92 | 33.14 | 1860 |
| Population baked 16, spaced | 25 | 62.4 | 5.2 | 33.91 | 34.35 | 1862 |
| Population baked 16, overlap | 23 | 62.4 | 5.2 | 38.01 | 39.24 | 1850 |
| Population direct 16, spaced | 0 | 0 | 0 | 35.64 | 36.21 | 1868 |
| Population direct 16, overlap | 0 | 0 | 0 | 39.44 | 39.90 | 1871 |

The baker payload is byte-identical to the Metal slice, as it should be for
deterministic occupancy. Startup baking costs about 25 µs at density one and
116 µs at density two. At one copy, static or moving, the baked scenes end
3.6–4.5 MiB below the matching direct scene in working set; the 16-copy cases
narrow that to 1.4–1.7 MiB.
Overlap adds about 4 MiB for either representation. Windows working-set deltas
and macOS resident deltas use different accounting and must not be compared
across hosts. The NVX column moves by 26 MiB between cases with no order by
representation, copy count or motion. At this fixture's scale it measures the
desktop's other GPU clients, not this scene, so OpenGL leaves process GPU
residency unwitnessed (`gpu_process_allocated_bytes` is 0, meaning
unavailable).

## Whole-frame and GPU stage costs

Steady means exclude each report's warmup. Stage columns are timing-on
sampled timeline intervals in ms, not additive exclusive costs. `—` means
the stage did not run.

| Case | Off steady mean ms | Off steady p95 ms | On steady mean ms | voxelPerAxisFinalize | voxelPerAxisOverflow | computeSunShadow | lightingOverflow | shapeDepth | shapeOwnerElect |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Density baked 1 | 2.320 | 3.120 | 2.783 | 0.540 | 0.136 | 0.104 | 0.054 | — | — |
| Density baked 2 | 2.553 | 3.153 | 2.880 | 0.535 | 0.136 | 0.103 | 0.053 | — | — |
| Density direct 1 | 2.747 | 3.530 | 3.417 | 0.533 | 0.134 | 0.177 | 0.033 | 0.047 | 0.046 |
| Motion baked, parented | 2.597 | 3.377 | 2.780 | 0.536 | 0.138 | 0.105 | 0.058 | — | — |
| Motion baked, root | 2.470 | 3.220 | 2.773 | 0.535 | 0.138 | 0.106 | 0.059 | — | — |
| Motion direct, parented | 3.407 | 4.237 | 4.090 | 0.560 | 0.140 | 0.179 | 0.057 | 0.230 | 0.222 |
| Motion direct, root | 3.213 | 4.110 | 4.107 | 0.564 | 0.137 | 0.182 | 0.033 | 0.222 | 0.227 |
| Population baked 16, spaced | 3.380 | 8.350 | 3.760 | 0.726 | 0.288 | 0.149 | 0.064 | — | — |
| Population baked 16, overlap | 5.557 | 8.530 | 6.350 | 0.785 | 0.392 | 1.209 | 1.159 | — | — |
| Population direct 16, spaced | 3.697 | 5.223 | 4.433 | 0.683 | 0.183 | 0.315 | 0.057 | 0.119 | 0.121 |
| Population direct 16, overlap | 4.920 | 5.623 | 5.670 | 0.691 | 0.203 | 0.319 | 0.082 | 0.453 | 0.480 |

No measured frame exceeded the 60 Hz budget. In this fixture the baked
subjects have a lower steady mean than direct SDF at one copy, static or
moving, and at sixteen spaced copies. Two results reverse that:

- **Overlapping baked copies cost more than overlapping direct copies**
  (5.56 vs 4.92 ms). The cost sits in the per-axis overflow lane: 11 660
  overflow entries against 3 296 when spaced, with `computeSunShadow` and
  `lightingOverflow` each near 1.2 ms. Direct overlap pays instead in SDF
  depth and owner election (about 0.45 ms each). This matches the
  overlap-specific baked increase in the
  [AC Release qualification](../ac-release-qualification/README.md).
- **Sixteen baked copies have a heavy tail.** Their steady p95 is about 8.4 ms
  in both layouts, against 5.2–5.6 ms for direct, even where the baked mean
  is lower.

Moving a baked subject, as root or parented, costs within 0.3 ms of the
static baked scene. Moving a direct subject raises SDF depth and owner
election from about 0.05 to 0.22 ms each. These are four-subject fixture
observations on one NVIDIA GL host, not a general throughput claim.

## Rejected attempts

- The first Release run of every `--sdf-voxel-scene` case crashed with
  SIGSEGV at the end of the auto-profile. `GL4API::glGetString` and
  `glGetStringi` were never loaded, so the `GL_NVX_gpu_memory_info` lookup
  behind `SDF-RESIDENCY` called a null pointer. This change fixes the loader
  and adds `GLApiLoader.*` tests; no measurement predates the fix.
- A first timing-off matrix stopped in round 3 on `QUIET=breach` (exit 76)
  when another session became active. Its partial results were discarded, and
  the timing-off matrix above is a complete fresh rerun.
