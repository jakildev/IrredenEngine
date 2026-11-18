| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.380 | 102.380–102.380 | n/a |
| frame p95 | 182.190 | 182.190–182.190 | n/a |
| frame p99 | 212.650 | 212.650–212.650 | n/a |
| steady frame avg | 91.930 | 91.930–91.930 | n/a |
| steady frame p95 | 116.790 | 116.790–116.790 | n/a |
| steady frame p99 | 150.200 | 150.200–150.200 | n/a |
| frame split INPUT | 0.148 | 0.148–0.148 | n/a |
| frame split UPDATE | 62.605 | 62.605–62.605 | n/a |
| frame split RENDER | 39.018 | 39.018–39.018 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.605 | 0.605–0.605 | n/a |
| GPU exclusive stage sum | 1.653 | 1.653–1.653 | n/a |
| GPU bakeSunShadowMap | 0.139 | 0.139–0.139 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.084 | 0.084–0.084 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.069 | 1.069–1.069 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.074 | 0.074–0.074 | n/a |
| GPU voxelStage2 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 91.930 | 116.791 | 150.202 | 161.338 |

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
