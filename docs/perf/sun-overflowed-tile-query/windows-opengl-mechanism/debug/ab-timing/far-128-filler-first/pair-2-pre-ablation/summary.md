| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 111.530 | 111.530–111.530 | n/a |
| frame p95 | 192.650 | 192.650–192.650 | n/a |
| frame p99 | 220.740 | 220.740–220.740 | n/a |
| steady frame avg | 101.700 | 101.700–101.700 | n/a |
| steady frame p95 | 131.550 | 131.550–131.550 | n/a |
| steady frame p99 | 143.440 | 143.440–143.440 | n/a |
| frame split INPUT | 0.162 | 0.162–0.162 | n/a |
| frame split UPDATE | 70.991 | 70.991–70.991 | n/a |
| frame split RENDER | 39.751 | 39.751–39.751 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.630 | 0.630–0.630 | n/a |
| GPU exclusive stage sum | 1.674 | 1.674–1.674 | n/a |
| GPU bakeSunShadowMap | 0.146 | 0.146–0.146 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.086 | 0.086–0.086 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.064 | 1.064–1.064 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.077 | 0.077–0.077 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.076 | 0.076–0.076 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.695 | 131.547 | 143.442 | 144.702 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.30 | 6.30–6.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
