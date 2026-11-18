| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.200 | 32.200–32.200 | n/a |
| frame p95 | 42.370 | 42.370–42.370 | n/a |
| frame p99 | 62.360 | 62.360–62.360 | n/a |
| steady frame avg | 30.880 | 30.880–30.880 | n/a |
| steady frame p95 | 31.640 | 31.640–31.640 | n/a |
| steady frame p99 | 32.630 | 32.630–32.630 | n/a |
| frame split INPUT | 0.294 | 0.294–0.294 | n/a |
| frame split UPDATE | 2.400 | 2.400–2.400 | n/a |
| frame split RENDER | 15.991 | 15.991–15.991 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 13.512 | 13.512–13.512 | n/a |
| GPU exclusive stage sum | 0.708 | 0.708–0.708 | n/a |
| GPU bakeSunShadowMap | 0.081 | 0.081–0.081 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.075 | 0.075–0.075 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.025 | 0.025–0.025 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.302 | 0.302–0.302 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.021 | 0.021–0.021 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.056 | 0.056–0.056 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 30.875 | 31.639 | 32.628 | 33.167 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
