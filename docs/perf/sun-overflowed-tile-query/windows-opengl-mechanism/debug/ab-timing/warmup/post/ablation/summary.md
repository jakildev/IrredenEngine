| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 43.610 | 43.610–43.610 | n/a |
| frame p95 | 100.150 | 100.150–100.150 | n/a |
| frame p99 | 121.890 | 121.890–121.890 | n/a |
| steady frame avg | 36.500 | 36.500–36.500 | n/a |
| steady frame p95 | 54.790 | 54.790–54.790 | n/a |
| steady frame p99 | 64.970 | 64.970–64.970 | n/a |
| frame split INPUT | 0.157 | 0.157–0.157 | n/a |
| frame split UPDATE | 2.864 | 2.864–2.864 | n/a |
| frame split RENDER | 40.392 | 40.392–40.392 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.202 | 0.202–0.202 | n/a |
| GPU exclusive stage sum | 1.150 | 1.150–1.150 | n/a |
| GPU bakeSunShadowMap | 0.082 | 0.082–0.082 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.077 | 0.077–0.077 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.026 | 0.026–0.026 | n/a |
| GPU computeVoxelAO | 0.007 | 0.007–0.007 | n/a |
| GPU entityCanvasToFb | 0.741 | 0.741–0.741 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.018 | 0.018–0.018 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.058 | 0.058–0.058 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 36.497 | 54.790 | 64.971 | 72.793 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.60 | 2.60–2.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
