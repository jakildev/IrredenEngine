| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 140.260 | 140.260–140.260 | n/a |
| frame p95 | 220.950 | 220.950–220.950 | n/a |
| frame p99 | 256.110 | 256.110–256.110 | n/a |
| steady frame avg | 128.920 | 128.920–128.920 | n/a |
| steady frame p95 | 165.010 | 165.010–165.010 | n/a |
| steady frame p99 | 178.690 | 178.690–178.690 | n/a |
| frame split INPUT | 0.250 | 0.250–0.250 | n/a |
| frame split UPDATE | 91.723 | 91.723–91.723 | n/a |
| frame split RENDER | 44.293 | 44.293–44.293 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 3.989 | 3.989–3.989 | n/a |
| GPU exclusive stage sum | 1.792 | 1.792–1.792 | n/a |
| GPU bakeSunShadowMap | 0.153 | 0.153–0.153 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.097 | 0.097–0.097 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.031 | 0.031–0.031 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.198 | 1.198–1.198 | n/a |
| GPU fbToScreen | 0.026 | 0.026–0.026 | n/a |
| GPU lightingToTrixel | 0.036 | 0.036–0.036 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.037 | 0.037–0.037 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.081 | 0.081–0.081 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 128.924 | 165.009 | 178.694 | 190.427 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.40 | 7.40–7.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
