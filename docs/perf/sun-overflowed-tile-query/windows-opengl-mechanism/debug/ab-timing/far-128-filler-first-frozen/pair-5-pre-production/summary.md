| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 134.820 | 134.820–134.820 | n/a |
| frame p95 | 216.650 | 216.650–216.650 | n/a |
| frame p99 | 259.960 | 259.960–259.960 | n/a |
| steady frame avg | 126.950 | 126.950–126.950 | n/a |
| steady frame p95 | 156.030 | 156.030–156.030 | n/a |
| steady frame p99 | 176.210 | 176.210–176.210 | n/a |
| frame split INPUT | 0.235 | 0.235–0.235 | n/a |
| frame split UPDATE | 90.259 | 90.259–90.259 | n/a |
| frame split RENDER | 42.137 | 42.137–42.137 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.193 | 2.193–2.193 | n/a |
| GPU exclusive stage sum | 2.050 | 2.050–2.050 | n/a |
| GPU bakeSunShadowMap | 0.155 | 0.155–0.155 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.132 | 0.132–0.132 | n/a |
| GPU computeLightVolume | 0.038 | 0.038–0.038 | n/a |
| GPU computeSunShadow | 0.054 | 0.054–0.054 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.292 | 1.292–1.292 | n/a |
| GPU fbToScreen | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.028 | 0.028–0.028 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.096 | 0.096–0.096 | n/a |
| GPU voxelStage2 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelSunFaces | 0.082 | 0.082–0.082 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 126.945 | 156.034 | 176.210 | 185.832 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.30 | 7.30–7.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
