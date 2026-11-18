| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 148.030 | 148.030–148.030 | n/a |
| frame p95 | 212.370 | 212.370–212.370 | n/a |
| frame p99 | 249.700 | 249.700–249.700 | n/a |
| steady frame avg | 138.100 | 138.100–138.100 | n/a |
| steady frame p95 | 173.380 | 173.380–173.380 | n/a |
| steady frame p99 | 188.870 | 188.870–188.870 | n/a |
| frame split INPUT | 0.267 | 0.267–0.267 | n/a |
| frame split UPDATE | 99.054 | 99.054–99.054 | n/a |
| frame split RENDER | 44.347 | 44.347–44.347 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.365 | 4.365–4.365 | n/a |
| GPU exclusive stage sum | 2.202 | 2.202–2.202 | n/a |
| GPU bakeSunShadowMap | 0.162 | 0.162–0.162 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.503 | 1.503–1.503 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.093 | 0.093–0.093 | n/a |
| GPU voxelStage2 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelSunFaces | 0.087 | 0.087–0.087 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 138.101 | 173.380 | 188.871 | 209.674 |

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
