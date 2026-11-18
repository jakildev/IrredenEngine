| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 96.900 | 96.900–96.900 | n/a |
| frame p95 | 101.980 | 101.980–101.980 | n/a |
| frame p99 | 128.350 | 128.350–128.350 | n/a |
| steady frame avg | 96.300 | 96.300–96.300 | n/a |
| steady frame p95 | 101.880 | 101.880–101.880 | n/a |
| steady frame p99 | 104.780 | 104.780–104.780 | n/a |
| frame split INPUT | 0.819 | 0.819–0.819 | n/a |
| frame split UPDATE | 6.776 | 6.776–6.776 | n/a |
| frame split RENDER | 16.032 | 16.032–16.032 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 73.274 | 73.274–73.274 | n/a |
| GPU exclusive stage sum | 66.233 | 66.233–66.233 | n/a |
| GPU bakeSunShadowMap | 0.082 | 0.082–0.082 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.115 | 0.115–0.115 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.043 | 0.043–0.043 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 65.755 | 65.755–65.755 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.054 | 0.054–0.054 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 96.303 | 101.880 | 104.785 | 106.688 |

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
