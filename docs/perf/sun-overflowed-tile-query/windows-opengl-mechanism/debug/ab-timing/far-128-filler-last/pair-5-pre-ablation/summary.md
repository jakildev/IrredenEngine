| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 142.570 | 142.570–142.570 | n/a |
| frame p95 | 210.500 | 210.500–210.500 | n/a |
| frame p99 | 244.940 | 244.940–244.940 | n/a |
| steady frame avg | 133.620 | 133.620–133.620 | n/a |
| steady frame p95 | 167.270 | 167.270–167.270 | n/a |
| steady frame p99 | 177.960 | 177.960–177.960 | n/a |
| frame split INPUT | 0.254 | 0.254–0.254 | n/a |
| frame split UPDATE | 93.663 | 93.663–93.663 | n/a |
| frame split RENDER | 44.502 | 44.502–44.502 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.146 | 4.146–4.146 | n/a |
| GPU exclusive stage sum | 1.839 | 1.839–1.839 | n/a |
| GPU bakeSunShadowMap | 0.163 | 0.163–0.163 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.096 | 0.096–0.096 | n/a |
| GPU computeLightVolume | 0.039 | 0.039–0.039 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.231 | 1.231–1.231 | n/a |
| GPU fbToScreen | 0.024 | 0.024–0.024 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.039 | 0.039–0.039 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.083 | 0.083–0.083 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.622 | 167.273 | 177.962 | 209.490 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.50 | 7.50–7.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
