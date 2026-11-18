| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 106.680 | 106.680–106.680 | n/a |
| frame p95 | 200.230 | 200.230–200.230 | n/a |
| frame p99 | 235.900 | 235.900–235.900 | n/a |
| steady frame avg | 93.840 | 93.840–93.840 | n/a |
| steady frame p95 | 122.640 | 122.640–122.640 | n/a |
| steady frame p99 | 135.250 | 135.250–135.250 | n/a |
| frame split INPUT | 0.161 | 0.161–0.161 | n/a |
| frame split UPDATE | 65.723 | 65.723–65.723 | n/a |
| frame split RENDER | 40.138 | 40.138–40.138 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.660 | 0.660–0.660 | n/a |
| GPU exclusive stage sum | 1.929 | 1.929–1.929 | n/a |
| GPU bakeSunShadowMap | 0.136 | 0.136–0.136 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.049 | 0.049–0.049 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.275 | 1.275–1.275 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.077 | 0.077–0.077 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 93.842 | 122.644 | 135.247 | 147.481 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.00 | 6.00–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
