| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 141.500 | 141.500–141.500 | n/a |
| frame p95 | 230.490 | 230.490–230.490 | n/a |
| frame p99 | 289.420 | 289.420–289.420 | n/a |
| steady frame avg | 133.430 | 133.430–133.430 | n/a |
| steady frame p95 | 171.110 | 171.110–171.110 | n/a |
| steady frame p99 | 184.490 | 184.490–184.490 | n/a |
| frame split INPUT | 0.259 | 0.259–0.259 | n/a |
| frame split UPDATE | 91.611 | 91.611–91.611 | n/a |
| frame split RENDER | 45.493 | 45.493–45.493 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.137 | 4.137–4.137 | n/a |
| GPU exclusive stage sum | 1.929 | 1.929–1.929 | n/a |
| GPU bakeSunShadowMap | 0.154 | 0.154–0.154 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.097 | 0.097–0.097 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.328 | 1.328–1.328 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.038 | 0.038–0.038 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.084 | 0.084–0.084 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.434 | 171.110 | 184.491 | 194.146 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.40 | 7.40–7.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
