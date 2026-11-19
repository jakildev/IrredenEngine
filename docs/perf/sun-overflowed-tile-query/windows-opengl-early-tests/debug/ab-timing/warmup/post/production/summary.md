| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 49.330 | 49.330–49.330 | n/a |
| frame p95 | 119.930 | 119.930–119.930 | n/a |
| frame p99 | 157.910 | 157.910–157.910 | n/a |
| steady frame avg | 41.440 | 41.440–41.440 | n/a |
| steady frame p95 | 47.010 | 47.010–47.010 | n/a |
| steady frame p99 | 53.350 | 53.350–53.350 | n/a |
| frame split INPUT | 0.107 | 0.107–0.107 | n/a |
| frame split UPDATE | 3.590 | 3.590–3.590 | n/a |
| frame split RENDER | 41.487 | 41.487–41.487 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.141 | 4.141–4.141 | n/a |
| GPU exclusive stage sum | 3.833 | 3.833–3.833 | n/a |
| GPU bakeSunShadowMap | 0.100 | 0.100–0.100 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.126 | 0.126–0.126 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 3.349 | 3.349–3.349 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.070 | 0.070–0.070 | n/a |
| input_to_present mean | 78.357 | 78.357–78.357 | n/a |
| input_to_present p99 | 85.091 | 85.091–85.091 | n/a |
| cursor_to_present mean | 77.135 | 77.135–77.135 | n/a |
| cursor_to_present p99 | 83.874 | 83.874–83.874 | n/a |
| frame_start_to_submit mean | 36.784 | 36.784–36.784 | n/a |
| frame_start_to_submit p99 | 53.295 | 53.295–53.295 | n/a |
| submit_to_present mean | 43.620 | 43.620–43.620 | n/a |
| submit_to_present p99 | 56.136 | 56.136–56.136 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.436 | 47.007 | 53.351 | 59.370 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1033 |

Render device counters: unsupported by this backend.
