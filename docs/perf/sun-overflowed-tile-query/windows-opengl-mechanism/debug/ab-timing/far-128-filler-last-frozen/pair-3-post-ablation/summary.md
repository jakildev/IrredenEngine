| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 122.940 | 122.940–122.940 | n/a |
| frame p95 | 196.270 | 196.270–196.270 | n/a |
| frame p99 | 220.810 | 220.810–220.810 | n/a |
| steady frame avg | 113.990 | 113.990–113.990 | n/a |
| steady frame p95 | 158.110 | 158.110–158.110 | n/a |
| steady frame p99 | 182.320 | 182.320–182.320 | n/a |
| frame split INPUT | 0.197 | 0.197–0.197 | n/a |
| frame split UPDATE | 77.885 | 77.885–77.885 | n/a |
| frame split RENDER | 43.347 | 43.347–43.347 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.511 | 1.511–1.511 | n/a |
| GPU exclusive stage sum | 1.784 | 1.784–1.784 | n/a |
| GPU bakeSunShadowMap | 0.137 | 0.137–0.137 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.088 | 0.088–0.088 | n/a |
| GPU computeLightVolume | 0.036 | 0.036–0.036 | n/a |
| GPU computeSunShadow | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.243 | 1.243–1.243 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.034 | 0.034–0.034 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 113.988 | 158.114 | 182.319 | 194.018 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.80 | 6.80–6.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
