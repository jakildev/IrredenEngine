| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 41.730 | 41.730–41.730 | n/a |
| frame p95 | 44.250 | 44.250–44.250 | n/a |
| frame p99 | 69.350 | 69.350–69.350 | n/a |
| steady frame avg | 41.140 | 41.140–41.140 | n/a |
| steady frame p95 | 43.130 | 43.130–43.130 | n/a |
| steady frame p99 | 45.020 | 45.020–45.020 | n/a |
| frame split INPUT | 0.079 | 0.079–0.079 | n/a |
| frame split UPDATE | 3.073 | 3.073–3.073 | n/a |
| frame split RENDER | 15.800 | 15.800–15.800 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 22.779 | 22.779–22.779 | n/a |
| GPU exclusive stage sum | 3.795 | 3.795–3.795 | n/a |
| GPU bakeSunShadowMap | 0.105 | 0.105–0.105 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.146 | 0.146–0.146 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 | n/a |
| GPU entityCanvasToFb | 3.270 | 3.270–3.270 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.064 | 0.064–0.064 | n/a |
| input_to_present mean | 80.221 | 80.221–80.221 | n/a |
| input_to_present p99 | 85.264 | 85.264–85.264 | n/a |
| cursor_to_present mean | 79.044 | 79.044–79.044 | n/a |
| cursor_to_present p99 | 84.182 | 84.182–84.182 | n/a |
| frame_start_to_submit mean | 16.352 | 16.352–16.352 | n/a |
| frame_start_to_submit p99 | 24.542 | 24.542–24.542 | n/a |
| submit_to_present mean | 65.857 | 65.857–65.857 | n/a |
| submit_to_present p99 | 71.283 | 71.283–71.283 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.144 | 43.134 | 45.019 | 45.133 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.50 | 2.50–2.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0906 |

Render device counters: unsupported by this backend.
