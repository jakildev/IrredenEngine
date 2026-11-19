| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.250 | 32.250–32.250 | n/a |
| frame p95 | 33.170 | 33.170–33.170 | n/a |
| frame p99 | 55.970 | 55.970–55.970 | n/a |
| steady frame avg | 31.070 | 31.070–31.070 | n/a |
| steady frame p95 | 31.980 | 31.980–31.980 | n/a |
| steady frame p99 | 32.670 | 32.670–32.670 | n/a |
| frame split INPUT | 0.163 | 0.163–0.163 | n/a |
| frame split UPDATE | 2.438 | 2.438–2.438 | n/a |
| frame split RENDER | 14.698 | 14.698–14.698 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 14.953 | 14.953–14.953 | n/a |
| GPU exclusive stage sum | 0.701 | 0.701–0.701 | n/a |
| GPU bakeSunShadowMap | 0.083 | 0.083–0.083 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.075 | 0.075–0.075 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.017 | 0.017–0.017 | n/a |
| GPU entityCanvasToFb | 0.308 | 0.308–0.308 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.021 | 0.021–0.021 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.056 | 0.056–0.056 | n/a |
| input_to_present mean | 60.759 | 60.759–60.759 | n/a |
| input_to_present p99 | 63.216 | 63.216–63.216 | n/a |
| cursor_to_present mean | 59.465 | 59.465–59.465 | n/a |
| cursor_to_present p99 | 61.704 | 61.704–61.704 | n/a |
| frame_start_to_submit mean | 15.201 | 15.201–15.201 | n/a |
| frame_start_to_submit p99 | 20.491 | 20.491–20.491 | n/a |
| submit_to_present mean | 46.861 | 46.861–46.861 | n/a |
| submit_to_present p99 | 50.918 | 50.918–50.918 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 31.075 | 31.976 | 32.673 | 32.972 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0953 |

Render device counters: unsupported by this backend.
