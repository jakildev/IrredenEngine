| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 120.400 | 120.400–120.400 | n/a |
| frame p95 | 192.350 | 192.350–192.350 | n/a |
| frame p99 | 236.660 | 236.660–236.660 | n/a |
| steady frame avg | 112.160 | 112.160–112.160 | n/a |
| steady frame p95 | 144.380 | 144.380–144.380 | n/a |
| steady frame p99 | 157.630 | 157.630–157.630 | n/a |
| frame split INPUT | 0.188 | 0.188–0.188 | n/a |
| frame split UPDATE | 75.976 | 75.976–75.976 | n/a |
| frame split RENDER | 42.707 | 42.707–42.707 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.528 | 1.528–1.528 | n/a |
| GPU exclusive stage sum | 1.830 | 1.830–1.830 | n/a |
| GPU bakeSunShadowMap | 0.153 | 0.153–0.153 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.089 | 0.089–0.089 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.271 | 1.271–1.271 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.034 | 0.034–0.034 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.075 | 0.075–0.075 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 112.156 | 144.376 | 157.631 | 183.903 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.70 | 6.70–6.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
