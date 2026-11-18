| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 116.030 | 116.030–116.030 | n/a |
| frame p95 | 200.320 | 200.320–200.320 | n/a |
| frame p99 | 234.820 | 234.820–234.820 | n/a |
| steady frame avg | 106.490 | 106.490–106.490 | n/a |
| steady frame p95 | 134.220 | 134.220–134.220 | n/a |
| steady frame p99 | 164.150 | 164.150–164.150 | n/a |
| frame split INPUT | 0.184 | 0.184–0.184 | n/a |
| frame split UPDATE | 71.718 | 71.718–71.718 | n/a |
| frame split RENDER | 42.728 | 42.728–42.728 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.403 | 1.403–1.403 | n/a |
| GPU exclusive stage sum | 1.642 | 1.642–1.642 | n/a |
| GPU bakeSunShadowMap | 0.134 | 0.134–0.134 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.034 | 0.034–0.034 | n/a |
| GPU computeSunShadow | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.117 | 1.117–1.117 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.033 | 0.033–0.033 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 106.488 | 134.218 | 164.147 | 192.418 |

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
