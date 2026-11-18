| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 79.020 | 79.020–79.020 | n/a |
| frame p95 | 83.650 | 83.650–83.650 | n/a |
| frame p99 | 123.760 | 123.760–123.760 | n/a |
| steady frame avg | 78.200 | 78.200–78.200 | n/a |
| steady frame p95 | 82.670 | 82.670–82.670 | n/a |
| steady frame p99 | 85.770 | 85.770–85.770 | n/a |
| frame split INPUT | 0.665 | 0.665–0.665 | n/a |
| frame split UPDATE | 5.701 | 5.701–5.701 | n/a |
| frame split RENDER | 16.059 | 16.059–16.059 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 56.597 | 56.597–56.597 | n/a |
| GPU exclusive stage sum | 48.462 | 48.462–48.462 | n/a |
| GPU bakeSunShadowMap | 0.119 | 0.119–0.119 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.043 | 0.043–0.043 | n/a |
| GPU computeVoxelAO | 0.013 | 0.013–0.013 | n/a |
| GPU entityCanvasToFb | 47.941 | 47.941–47.941 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.028 | 0.028–0.028 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.054 | 0.054–0.054 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 78.199 | 82.672 | 85.766 | 87.864 |

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
