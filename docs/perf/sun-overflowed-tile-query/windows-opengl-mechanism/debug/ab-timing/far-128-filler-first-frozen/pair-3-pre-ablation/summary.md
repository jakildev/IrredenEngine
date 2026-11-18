| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 116.480 | 116.480–116.480 | n/a |
| frame p95 | 197.670 | 197.670–197.670 | n/a |
| frame p99 | 228.320 | 228.320–228.320 | n/a |
| steady frame avg | 106.520 | 106.520–106.520 | n/a |
| steady frame p95 | 135.840 | 135.840–135.840 | n/a |
| steady frame p99 | 154.780 | 154.780–154.780 | n/a |
| frame split INPUT | 0.184 | 0.184–0.184 | n/a |
| frame split UPDATE | 73.182 | 73.182–73.182 | n/a |
| frame split RENDER | 41.691 | 41.691–41.691 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.422 | 1.422–1.422 | n/a |
| GPU exclusive stage sum | 1.721 | 1.721–1.721 | n/a |
| GPU bakeSunShadowMap | 0.142 | 0.142–0.142 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.034 | 0.034–0.034 | n/a |
| GPU computeSunShadow | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.116 | 1.116–1.116 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.076 | 0.076–0.076 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.074 | 0.074–0.074 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 106.516 | 135.841 | 154.778 | 196.252 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.50 | 6.50–6.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
