| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 79.210 | 79.210–79.210 | n/a |
| frame p95 | 84.860 | 84.860–84.860 | n/a |
| frame p99 | 115.810 | 115.810–115.810 | n/a |
| steady frame avg | 78.650 | 78.650–78.650 | n/a |
| steady frame p95 | 84.240 | 84.240–84.240 | n/a |
| steady frame p99 | 87.040 | 87.040–87.040 | n/a |
| frame split INPUT | 0.646 | 0.646–0.646 | n/a |
| frame split UPDATE | 5.593 | 5.593–5.593 | n/a |
| frame split RENDER | 16.166 | 16.166–16.166 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 56.802 | 56.802–56.802 | n/a |
| GPU exclusive stage sum | 48.503 | 48.503–48.503 | n/a |
| GPU bakeSunShadowMap | 0.083 | 0.083–0.083 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.043 | 0.043–0.043 | n/a |
| GPU computeVoxelAO | 0.013 | 0.013–0.013 | n/a |
| GPU entityCanvasToFb | 48.019 | 48.019–48.019 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.055 | 0.055–0.055 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 78.648 | 84.235 | 87.039 | 88.479 |

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
