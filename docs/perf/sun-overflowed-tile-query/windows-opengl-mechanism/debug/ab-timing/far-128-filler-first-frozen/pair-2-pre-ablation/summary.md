| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 99.920 | 99.920–99.920 | n/a |
| frame p95 | 189.190 | 189.190–189.190 | n/a |
| frame p99 | 236.020 | 236.020–236.020 | n/a |
| steady frame avg | 89.370 | 89.370–89.370 | n/a |
| steady frame p95 | 121.590 | 121.590–121.590 | n/a |
| steady frame p99 | 141.980 | 141.980–141.980 | n/a |
| frame split INPUT | 0.140 | 0.140–0.140 | n/a |
| frame split UPDATE | 60.157 | 60.157–60.157 | n/a |
| frame split RENDER | 39.035 | 39.035–39.035 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.586 | 0.586–0.586 | n/a |
| GPU exclusive stage sum | 1.615 | 1.615–1.615 | n/a |
| GPU bakeSunShadowMap | 0.131 | 0.131–0.131 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.081 | 0.081–0.081 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.052 | 1.052–1.052 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.071 | 0.071–0.071 | n/a |
| GPU voxelStage2 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelSunFaces | 0.068 | 0.068–0.068 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 89.374 | 121.588 | 141.978 | 144.105 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.60 | 5.60–5.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
