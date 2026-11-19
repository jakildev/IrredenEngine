| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 126.760 | 126.760–126.760 | n/a |
| frame p95 | 207.110 | 207.110–207.110 | n/a |
| frame p99 | 247.410 | 247.410–247.410 | n/a |
| steady frame avg | 117.740 | 117.740–117.740 | n/a |
| steady frame p95 | 145.780 | 145.780–145.780 | n/a |
| steady frame p99 | 163.140 | 163.140–163.140 | n/a |
| frame split INPUT | 0.215 | 0.215–0.215 | n/a |
| frame split UPDATE | 82.463 | 82.463–82.463 | n/a |
| frame split RENDER | 42.016 | 42.016–42.016 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.066 | 2.066–2.066 | n/a |
| GPU exclusive stage sum | 2.118 | 2.118–2.118 | n/a |
| GPU bakeSunShadowMap | 0.151 | 0.151–0.151 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.128 | 0.128–0.128 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.053 | 0.053–0.053 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.383 | 1.383–1.383 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.093 | 0.093–0.093 | n/a |
| GPU voxelStage2 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelSunFaces | 0.081 | 0.081–0.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 117.740 | 145.784 | 163.144 | 168.525 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.00 | 7.00–7.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
