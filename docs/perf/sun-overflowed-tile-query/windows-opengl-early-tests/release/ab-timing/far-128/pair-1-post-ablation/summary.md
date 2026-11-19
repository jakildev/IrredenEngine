| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.010 | 39.010–39.010 | n/a |
| frame p95 | 40.810 | 40.810–40.810 | n/a |
| frame p99 | 74.300 | 74.300–74.300 | n/a |
| steady frame avg | 38.150 | 38.150–38.150 | n/a |
| steady frame p95 | 40.450 | 40.450–40.450 | n/a |
| steady frame p99 | 41.860 | 41.860–41.860 | n/a |
| frame split INPUT | 0.401 | 0.401–0.401 | n/a |
| frame split UPDATE | 2.856 | 2.856–2.856 | n/a |
| frame split RENDER | 15.494 | 15.494–15.494 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.263 | 20.263–20.263 | n/a |
| GPU exclusive stage sum | 0.807 | 0.807–0.807 | n/a |
| GPU bakeSunShadowMap | 0.105 | 0.105–0.105 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 | n/a |
| GPU entityCanvasToFb | 0.347 | 0.347–0.347 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.064 | 0.064–0.064 | n/a |
| input_to_present mean | 74.456 | 74.456–74.456 | n/a |
| input_to_present p99 | 79.152 | 79.152–79.152 | n/a |
| cursor_to_present mean | 73.284 | 73.284–73.284 | n/a |
| cursor_to_present p99 | 77.956 | 77.956–77.956 | n/a |
| frame_start_to_submit mean | 16.336 | 16.336–16.336 | n/a |
| frame_start_to_submit p99 | 28.189 | 28.189–28.189 | n/a |
| submit_to_present mean | 59.869 | 59.869–59.869 | n/a |
| submit_to_present p99 | 65.113 | 65.113–65.113 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.150 | 40.450 | 41.861 | 42.669 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.30 | 2.30–2.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1169 |

Render device counters: unsupported by this backend.
