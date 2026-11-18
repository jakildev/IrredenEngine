| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 123.710 | 123.710–123.710 | n/a |
| frame p95 | 191.960 | 191.960–191.960 | n/a |
| frame p99 | 235.380 | 235.380–235.380 | n/a |
| steady frame avg | 114.340 | 114.340–114.340 | n/a |
| steady frame p95 | 147.410 | 147.410–147.410 | n/a |
| steady frame p99 | 159.540 | 159.540–159.540 | n/a |
| frame split INPUT | 0.210 | 0.210–0.210 | n/a |
| frame split UPDATE | 80.308 | 80.308–80.308 | n/a |
| frame split RENDER | 41.122 | 41.122–41.122 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.075 | 2.075–2.075 | n/a |
| GPU exclusive stage sum | 1.813 | 1.813–1.813 | n/a |
| GPU bakeSunShadowMap | 0.148 | 0.148–0.148 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.089 | 0.089–0.089 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.031 | 0.031–0.031 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 1.163 | 1.163–1.163 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.092 | 0.092–0.092 | n/a |
| GPU voxelStage2 | 0.031 | 0.031–0.031 | n/a |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 114.344 | 147.408 | 159.539 | 163.028 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.90 | 6.90–6.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
