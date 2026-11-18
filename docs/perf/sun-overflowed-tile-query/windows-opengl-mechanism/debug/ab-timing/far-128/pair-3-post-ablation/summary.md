| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.680 | 46.680–46.680 | n/a |
| frame p95 | 107.470 | 107.470–107.470 | n/a |
| frame p99 | 129.280 | 129.280–129.280 | n/a |
| steady frame avg | 38.920 | 38.920–38.920 | n/a |
| steady frame p95 | 65.530 | 65.530–65.530 | n/a |
| steady frame p99 | 72.650 | 72.650–72.650 | n/a |
| frame split INPUT | 0.073 | 0.073–0.073 | n/a |
| frame split UPDATE | 3.186 | 3.186–3.186 | n/a |
| frame split RENDER | 43.018 | 43.018–43.018 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.405 | 0.405–0.405 | n/a |
| GPU exclusive stage sum | 1.391 | 1.391–1.391 | n/a |
| GPU bakeSunShadowMap | 0.083 | 0.083–0.083 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.077 | 0.077–0.077 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.007 | 0.007–0.007 | n/a |
| GPU entityCanvasToFb | 0.986 | 0.986–0.986 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.018 | 0.018–0.018 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.921 | 65.531 | 72.651 | 80.689 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
