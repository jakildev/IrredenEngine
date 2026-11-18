| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 148.510 | 148.510–148.510 | n/a |
| frame p95 | 257.990 | 257.990–257.990 | n/a |
| frame p99 | 298.720 | 298.720–298.720 | n/a |
| steady frame avg | 134.190 | 134.190–134.190 | n/a |
| steady frame p95 | 167.920 | 167.920–167.920 | n/a |
| steady frame p99 | 182.420 | 182.420–182.420 | n/a |
| frame split INPUT | 0.265 | 0.265–0.265 | n/a |
| frame split UPDATE | 97.328 | 97.328–97.328 | n/a |
| frame split RENDER | 46.627 | 46.627–46.627 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.288 | 4.288–4.288 | n/a |
| GPU exclusive stage sum | 2.437 | 2.437–2.437 | n/a |
| GPU bakeSunShadowMap | 0.160 | 0.160–0.160 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.143 | 0.143–0.143 | n/a |
| GPU computeLightVolume | 0.040 | 0.040–0.040 | n/a |
| GPU computeSunShadow | 0.057 | 0.057–0.057 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.646 | 1.646–1.646 | n/a |
| GPU fbToScreen | 0.024 | 0.024–0.024 | n/a |
| GPU lightingToTrixel | 0.058 | 0.058–0.058 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.096 | 0.096–0.096 | n/a |
| GPU voxelStage2 | 0.030 | 0.030–0.030 | n/a |
| GPU voxelSunFaces | 0.090 | 0.090–0.090 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 134.191 | 167.921 | 182.420 | 191.089 |

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
