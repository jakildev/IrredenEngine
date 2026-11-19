| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 117.860 | 117.860–117.860 | n/a |
| frame p95 | 199.420 | 199.420–199.420 | n/a |
| frame p99 | 238.010 | 238.010–238.010 | n/a |
| steady frame avg | 107.970 | 107.970–107.970 | n/a |
| steady frame p95 | 142.530 | 142.530–142.530 | n/a |
| steady frame p99 | 157.340 | 157.340–157.340 | n/a |
| frame split INPUT | 0.191 | 0.191–0.191 | n/a |
| frame split UPDATE | 74.934 | 74.934–74.934 | n/a |
| frame split RENDER | 41.264 | 41.264–41.264 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.475 | 1.475–1.475 | n/a |
| GPU exclusive stage sum | 1.847 | 1.847–1.847 | n/a |
| GPU bakeSunShadowMap | 0.148 | 0.148–0.148 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.091 | 0.091–0.091 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.212 | 1.212–1.212 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.083 | 0.083–0.083 | n/a |
| GPU voxelStage2 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelSunFaces | 0.079 | 0.079–0.079 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 107.967 | 142.530 | 157.340 | 161.689 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.60 | 6.60–6.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
