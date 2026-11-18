| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 115.780 | 115.780–115.780 | n/a |
| frame p95 | 200.260 | 200.260–200.260 | n/a |
| frame p99 | 251.810 | 251.810–251.810 | n/a |
| steady frame avg | 106.250 | 106.250–106.250 | n/a |
| steady frame p95 | 142.590 | 142.590–142.590 | n/a |
| steady frame p99 | 166.670 | 166.670–166.670 | n/a |
| frame split INPUT | 0.184 | 0.184–0.184 | n/a |
| frame split UPDATE | 72.638 | 72.638–72.638 | n/a |
| frame split RENDER | 41.467 | 41.467–41.467 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.489 | 1.489–1.489 | n/a |
| GPU exclusive stage sum | 1.885 | 1.885–1.885 | n/a |
| GPU bakeSunShadowMap | 0.136 | 0.136–0.136 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.115 | 0.115–0.115 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.049 | 0.049–0.049 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.244 | 1.244–1.244 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.072 | 0.072–0.072 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 106.250 | 142.586 | 166.671 | 177.558 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.40 | 6.40–6.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
