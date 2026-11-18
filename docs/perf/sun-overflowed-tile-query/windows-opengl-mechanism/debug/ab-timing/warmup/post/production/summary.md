| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 82.030 | 82.030–82.030 | n/a |
| frame p95 | 103.020 | 103.020–103.020 | n/a |
| frame p99 | 191.510 | 191.510–191.510 | n/a |
| steady frame avg | 79.500 | 79.500–79.500 | n/a |
| steady frame p95 | 94.970 | 94.970–94.970 | n/a |
| steady frame p99 | 101.660 | 101.660–101.660 | n/a |
| frame split INPUT | 1.104 | 1.104–1.104 | n/a |
| frame split UPDATE | 5.521 | 5.521–5.521 | n/a |
| frame split RENDER | 36.474 | 36.474–36.474 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 38.932 | 38.932–38.932 | n/a |
| GPU exclusive stage sum | 49.358 | 49.358–49.358 | n/a |
| GPU bakeSunShadowMap | 0.083 | 0.083–0.083 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.115 | 0.115–0.115 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.043 | 0.043–0.043 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 48.888 | 48.888–48.888 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.055 | 0.055–0.055 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 79.500 | 94.968 | 101.662 | 103.922 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.80 | 4.80–4.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
