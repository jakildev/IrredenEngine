| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 103.330 | 103.330–103.330 | n/a |
| frame p95 | 187.220 | 187.220–187.220 | n/a |
| frame p99 | 214.990 | 214.990–214.990 | n/a |
| steady frame avg | 93.050 | 93.050–93.050 | n/a |
| steady frame p95 | 118.740 | 118.740–118.740 | n/a |
| steady frame p99 | 155.780 | 155.780–155.780 | n/a |
| frame split INPUT | 0.146 | 0.146–0.146 | n/a |
| frame split UPDATE | 62.934 | 62.934–62.934 | n/a |
| frame split RENDER | 39.617 | 39.617–39.617 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.631 | 0.631–0.631 | n/a |
| GPU exclusive stage sum | 1.741 | 1.741–1.741 | n/a |
| GPU bakeSunShadowMap | 0.136 | 0.136–0.136 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.176 | 1.176–1.176 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.036 | 0.036–0.036 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.068 | 0.068–0.068 | n/a |
| GPU voxelStage2 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 93.048 | 118.739 | 155.778 | 168.430 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.80 | 5.80–5.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
