| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 106.250 | 106.250–106.250 | n/a |
| frame p95 | 197.470 | 197.470–197.470 | n/a |
| frame p99 | 247.870 | 247.870–247.870 | n/a |
| steady frame avg | 92.780 | 92.780–92.780 | n/a |
| steady frame p95 | 124.410 | 124.410–124.410 | n/a |
| steady frame p99 | 148.780 | 148.780–148.780 | n/a |
| frame split INPUT | 0.158 | 0.158–0.158 | n/a |
| frame split UPDATE | 65.491 | 65.491–65.491 | n/a |
| frame split RENDER | 39.950 | 39.950–39.950 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.653 | 0.653–0.653 | n/a |
| GPU exclusive stage sum | 1.841 | 1.841–1.841 | n/a |
| GPU bakeSunShadowMap | 0.142 | 0.142–0.142 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.243 | 1.243–1.243 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.076 | 0.076–0.076 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.075 | 0.075–0.075 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 92.784 | 124.412 | 148.776 | 192.182 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.90 | 5.90–5.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
