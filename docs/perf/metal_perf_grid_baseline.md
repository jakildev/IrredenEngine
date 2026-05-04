# Perf Grid Baseline

Date: 2026-05-03

Host: macOS 25.2.0, Apple M4 Max, Metal backend

Command:

```sh
cmake --preset macos-debug
cmake --build build --target IRPerfGrid -- -j8
./IRPerfGrid --auto-profile 600 --mode voxel_set
./IRPerfGrid --auto-profile 600 --mode sdf
```

## Notes

`IRPerfGrid` creates a 64x64x64 grid, which is 262,144 entities. In `voxel_set`
mode this consumes the full global single-voxel pool. Both modes enable the full
lighting render pipeline and periodic-idle wave motion.

Metal timing probes stage-boundary counter sampling and emits samples through
pass descriptors, rather than falling back to `finish()` around tagged systems.
GPU-stage rows below are real Metal counter samples for stages that encode GPU
work. CPU-only tagged systems such as `ComputeLightVolume` show `0.000 ms` in
GPU timing and are broken down separately in the CPU phase table.

OpenGL was configured and built in `build-opengl`, but macOS failed to create a
GL context:

```text
GLFW error 65543: NSGL: The targeted version of macOS only supports forward-compatible core profile contexts for OpenGL 3.2 and above
Failed to create window: glfwCreateWindow returned null
```

OpenGL comparison numbers still need to be captured from the Linux/Windows
OpenGL host.

## Metal Results

| Mode | Avg frame | p95 | p99 | Entity count |
| --- | ---: | ---: | ---: | ---: |
| `voxel_set` | 106.31 ms | 110.13 ms | 114.31 ms | 262,303 |
| `sdf` | 91.31 ms | 94.61 ms | 98.05 ms | 262,303 |

Top CPU systems:

| Mode | System | Avg |
| --- | --- | ---: |
| `voxel_set` | `ComputeLightVolume` | 72.123 ms |
| `voxel_set` | `SingleVoxelToCanvasFirst` | 4.710 ms |
| `voxel_set` | `PeriodicIdle` | 1.980 ms/update tick |
| `voxel_set` | `UpdateVoxelSetChildren` | 1.074 ms/update tick |
| `sdf` | `ComputeLightVolume` | 72.225 ms |
| `sdf` | `ShapesToTrixel` | 1.478 ms |
| `sdf` | `PeriodicIdle` | 1.789 ms/update tick |

Metal GPU-stage snapshot from the final frame:

| Stage | `voxel_set` | `sdf` |
| --- | ---: | ---: |
| `voxelStage1` | 0.206 ms | 0.000 ms |
| `voxelStage2` | 1.590 ms | 0.000 ms |
| `shapePass1` | 0.000 ms | 1.060 ms |
| `computeLightVolume` | 0.000 ms | 0.000 ms |
| `lightingToTrixel` | 0.050 ms | 0.082 ms |
| `trixelToFb` | 0.123 ms | 0.125 ms |
| `screenSpaceResidualRotate` | 0.155 ms | 0.176 ms |

`ComputeLightVolume` CPU phase breakdown:

| Mode | Clear | Populate | Upload |
| --- | ---: | ---: | ---: |
| `voxel_set` | 0.034 ms | 71.194 ms | 0.888 ms |
| `sdf` | 0.035 ms | 71.298 ms | 0.885 ms |

## Phase Tracking

| Phase | Mode | Avg frame | `ComputeLightVolume` CPU | Populate | Upload | Key note |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Initial baseline | `voxel_set` | 107.17 ms | 72.280 ms | n/a | n/a | Legacy finish-bracketed GPU timing inflated stage attribution. |
| Initial baseline | `sdf` | 93.97 ms | 71.674 ms | n/a | n/a | Legacy finish-bracketed GPU timing inflated stage attribution. |
| Phase 0a-0c | `voxel_set` | 106.31 ms | 72.123 ms | 71.194 ms | 0.888 ms | Real Metal counter samples; bottleneck is CPU populate. |
| Phase 0a-0c | `sdf` | 91.31 ms | 72.225 ms | 71.298 ms | 0.885 ms | Real Metal counter samples; bottleneck is CPU populate. |
| Phase 1a | `voxel_set` | 12.28 ms | 0.039 ms | n/a (GPU) | 0.012 ms | GPU light-volume rewrite — CPU populate replaced by 32-iter compute dilation; 8.7x frame-time win. |
| Phase 1a | `sdf` | 8.63 ms | 0.039 ms | n/a (GPU) | 0.012 ms | GPU light-volume rewrite — same dilation chain on the SDF path; 10.6x frame-time win. |

### Phase 1a notes (GPU light volume rewrite)

`ComputeLightVolume` is now driven by a three-pass GPU compute chain
(`c_clear_light_volume` → `c_seed_light_volume` → 32 ×
`c_propagate_light_volume`) operating on a ping-pong pair of 128³ RGBA8
3D textures (`C_CanvasLightVolume`). Light sources are uploaded once per
frame to a small `LightSourceBuffer` SSBO (capped at 256 lights, ~16 KiB
per upload); propagation parameters live in a 16-byte
`LightVolumeParams` UBO. Alpha tracks residual strength so the falloff
stays linear and clamps cleanly at the radius edge — visual character
matches the CPU BFS to a first approximation. The Metal `[[buffer(N)]]`
slots for the new SSBO + UBO had to be moved into the 0–30 range
(landed at 4 and 23) because Apple Silicon caps compute-shader buffer
bindings at 31 slots; the C++ binding constants and both backends now
agree.

GPU `computeLightVolume` stage timing now reads ~0.04–0.05 ms — the
work is wholly on the GPU and the CPU side is just SSBO upload +
dispatch bookkeeping. Phase 1c will revisit the global radius cap
(currently `1 / stepFalloff_` = 32 cells) to support per-light radius
variation and channel-mixing for overlapping lights.

## Initial Read

The dominant bottleneck in both modes is `ComputeLightVolume`, around 72 ms per
render frame. Phase 0 shows that this is almost entirely CPU flood-fill populate
work, not GPU dispatch time or texture upload. The next optimization PR should
replace the CPU producer with GPU light-volume propagation before spending time
on smaller render-stage costs.

The renderer-specific split still appears in the secondary costs: voxel mode
spends about 0.2 ms in `voxelStage1` and 1.6 ms in `voxelStage2`, while SDF mode
spends about 1.1 ms in `shapePass1`.
