| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 108.520 | 108.520–108.520 | n/a |
| frame p95 | 188.420 | 188.420–188.420 | n/a |
| frame p99 | 235.190 | 235.190–235.190 | n/a |
| steady frame avg | 101.240 | 101.240–101.240 | n/a |
| steady frame p95 | 132.730 | 132.730–132.730 | n/a |
| steady frame p99 | 142.620 | 142.620–142.620 | n/a |
| frame split INPUT | 0.157 | 0.157–0.157 | n/a |
| frame split UPDATE | 68.510 | 68.510–68.510 | n/a |
| frame split RENDER | 39.241 | 39.241–39.241 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.609 | 0.609–0.609 | n/a |
| GPU exclusive stage sum | 1.796 | 1.796–1.796 | n/a |
| GPU bakeSunShadowMap | 0.137 | 0.137–0.137 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.081 | 0.081–0.081 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.207 | 1.207–1.207 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.078 | 0.078–0.078 | n/a |
| GPU voxelStage2 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.244 | 132.726 | 142.622 | 151.128 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
