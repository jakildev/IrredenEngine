| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 79.300 | 79.300–79.300 | n/a |
| frame p95 | 83.920 | 83.920–83.920 | n/a |
| frame p99 | 123.240 | 123.240–123.240 | n/a |
| steady frame avg | 78.370 | 78.370–78.370 | n/a |
| steady frame p95 | 82.770 | 82.770–82.770 | n/a |
| steady frame p99 | 85.320 | 85.320–85.320 | n/a |
| frame split INPUT | 0.656 | 0.656–0.656 | n/a |
| frame split UPDATE | 5.666 | 5.666–5.666 | n/a |
| frame split RENDER | 15.780 | 15.780–15.780 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 57.203 | 57.203–57.203 | n/a |
| GPU exclusive stage sum | 48.579 | 48.579–48.579 | n/a |
| GPU bakeSunShadowMap | 0.082 | 0.082–0.082 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.114 | 0.114–0.114 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.043 | 0.043–0.043 | n/a |
| GPU computeVoxelAO | 0.014 | 0.014–0.014 | n/a |
| GPU entityCanvasToFb | 48.096 | 48.096–48.096 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.027 | 0.027–0.027 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.054 | 0.054–0.054 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 78.370 | 82.772 | 85.320 | 86.800 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.70 | 4.70–4.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
