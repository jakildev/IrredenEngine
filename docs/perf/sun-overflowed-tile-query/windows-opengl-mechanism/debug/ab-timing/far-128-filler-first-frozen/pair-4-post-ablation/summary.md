| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 142.790 | 142.790–142.790 | n/a |
| frame p95 | 213.330 | 213.330–213.330 | n/a |
| frame p99 | 253.410 | 253.410–253.410 | n/a |
| steady frame avg | 133.340 | 133.340–133.340 | n/a |
| steady frame p95 | 165.790 | 165.790–165.790 | n/a |
| steady frame p99 | 180.900 | 180.900–180.900 | n/a |
| frame split INPUT | 0.263 | 0.263–0.263 | n/a |
| frame split UPDATE | 94.561 | 94.561–94.561 | n/a |
| frame split RENDER | 43.950 | 43.950–43.950 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.017 | 4.017–4.017 | n/a |
| GPU exclusive stage sum | 2.131 | 2.131–2.131 | n/a |
| GPU bakeSunShadowMap | 0.161 | 0.161–0.161 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.042 | 0.042–0.042 | n/a |
| GPU computeSunShadow | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.418 | 1.418–1.418 | n/a |
| GPU fbToScreen | 0.024 | 0.024–0.024 | n/a |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.100 | 0.100–0.100 | n/a |
| GPU voxelStage2 | 0.031 | 0.031–0.031 | n/a |
| GPU voxelSunFaces | 0.085 | 0.085–0.085 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.341 | 165.790 | 180.896 | 182.913 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.60 | 7.60–7.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
