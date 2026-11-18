| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 140.580 | 140.580–140.580 | n/a |
| frame p95 | 212.770 | 212.770–212.770 | n/a |
| frame p99 | 252.300 | 252.300–252.300 | n/a |
| steady frame avg | 133.340 | 133.340–133.340 | n/a |
| steady frame p95 | 167.400 | 167.400–167.400 | n/a |
| steady frame p99 | 180.350 | 180.350–180.350 | n/a |
| frame split INPUT | 0.253 | 0.253–0.253 | n/a |
| frame split UPDATE | 91.448 | 91.448–91.448 | n/a |
| frame split RENDER | 44.950 | 44.950–44.950 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 3.929 | 3.929–3.929 | n/a |
| GPU exclusive stage sum | 2.022 | 2.022–2.022 | n/a |
| GPU bakeSunShadowMap | 0.164 | 0.164–0.164 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.096 | 0.096–0.096 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.411 | 1.411–1.411 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.040 | 0.040–0.040 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.084 | 0.084–0.084 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.344 | 167.400 | 180.352 | 187.938 |

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
