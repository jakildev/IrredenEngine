| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 120.920 | 120.920–120.920 | n/a |
| frame p95 | 214.950 | 214.950–214.950 | n/a |
| frame p99 | 250.740 | 250.740–250.740 | n/a |
| steady frame avg | 110.680 | 110.680–110.680 | n/a |
| steady frame p95 | 144.780 | 144.780–144.780 | n/a |
| steady frame p99 | 159.930 | 159.930–159.930 | n/a |
| frame split INPUT | 0.204 | 0.204–0.204 | n/a |
| frame split UPDATE | 77.534 | 77.534–77.534 | n/a |
| frame split RENDER | 41.280 | 41.280–41.280 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.906 | 1.906–1.906 | n/a |
| GPU exclusive stage sum | 1.977 | 1.977–1.977 | n/a |
| GPU bakeSunShadowMap | 0.146 | 0.146–0.146 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.087 | 0.087–0.087 | n/a |
| GPU computeLightVolume | 0.034 | 0.034–0.034 | n/a |
| GPU computeSunShadow | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.345 | 1.345–1.345 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.086 | 0.086–0.086 | n/a |
| GPU voxelStage2 | 0.027 | 0.027–0.027 | n/a |
| GPU voxelSunFaces | 0.076 | 0.076–0.076 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 110.678 | 144.783 | 159.926 | 167.156 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.70 | 6.70–6.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
