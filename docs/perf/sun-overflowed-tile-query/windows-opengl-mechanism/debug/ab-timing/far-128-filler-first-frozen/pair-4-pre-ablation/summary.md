| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 146.630 | 146.630–146.630 | n/a |
| frame p95 | 224.770 | 224.770–224.770 | n/a |
| frame p99 | 263.300 | 263.300–263.300 | n/a |
| steady frame avg | 137.810 | 137.810–137.810 | n/a |
| steady frame p95 | 173.180 | 173.180–173.180 | n/a |
| steady frame p99 | 182.150 | 182.150–182.150 | n/a |
| frame split INPUT | 0.267 | 0.267–0.267 | n/a |
| frame split UPDATE | 97.704 | 97.704–97.704 | n/a |
| frame split RENDER | 44.424 | 44.424–44.424 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.230 | 4.230–4.230 | n/a |
| GPU exclusive stage sum | 1.992 | 1.992–1.992 | n/a |
| GPU bakeSunShadowMap | 0.160 | 0.160–0.160 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.097 | 0.097–0.097 | n/a |
| GPU computeLightVolume | 0.038 | 0.038–0.038 | n/a |
| GPU computeSunShadow | 0.033 | 0.033–0.033 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.280 | 1.280–1.280 | n/a |
| GPU fbToScreen | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.101 | 0.101–0.101 | n/a |
| GPU voxelStage2 | 0.032 | 0.032–0.032 | n/a |
| GPU voxelSunFaces | 0.086 | 0.086–0.086 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 137.814 | 173.178 | 182.152 | 199.398 |

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
