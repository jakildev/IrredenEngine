| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 145.400 | 145.400–145.400 | n/a |
| frame p95 | 219.750 | 219.750–219.750 | n/a |
| frame p99 | 259.160 | 259.160–259.160 | n/a |
| steady frame avg | 135.560 | 135.560–135.560 | n/a |
| steady frame p95 | 169.430 | 169.430–169.430 | n/a |
| steady frame p99 | 189.070 | 189.070–189.070 | n/a |
| frame split INPUT | 0.267 | 0.267–0.267 | n/a |
| frame split UPDATE | 95.984 | 95.984–95.984 | n/a |
| frame split RENDER | 44.926 | 44.926–44.926 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.221 | 4.221–4.221 | n/a |
| GPU exclusive stage sum | 1.939 | 1.939–1.939 | n/a |
| GPU bakeSunShadowMap | 0.157 | 0.157–0.157 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.098 | 0.098–0.098 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.340 | 1.340–1.340 | n/a |
| GPU fbToScreen | 0.024 | 0.024–0.024 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.037 | 0.037–0.037 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.082 | 0.082–0.082 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 135.562 | 169.431 | 189.065 | 200.450 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.60 | 7.60–7.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
