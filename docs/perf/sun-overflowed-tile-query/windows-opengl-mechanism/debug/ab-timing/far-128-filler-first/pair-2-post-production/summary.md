| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.990 | 102.990–102.990 | n/a |
| frame p95 | 180.820 | 180.820–180.820 | n/a |
| frame p99 | 208.380 | 208.380–208.380 | n/a |
| steady frame avg | 94.710 | 94.710–94.710 | n/a |
| steady frame p95 | 144.360 | 144.360–144.360 | n/a |
| steady frame p99 | 173.020 | 173.020–173.020 | n/a |
| frame split INPUT | 0.302 | 0.302–0.302 | n/a |
| frame split UPDATE | 62.767 | 62.767–62.767 | n/a |
| frame split RENDER | 39.181 | 39.181–39.181 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.737 | 0.737–0.737 | n/a |
| GPU exclusive stage sum | 39.212 | 39.212–39.212 | n/a |
| GPU bakeSunShadowMap | 0.128 | 0.128–0.128 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.120 | 0.120–0.120 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 38.629 | 38.629–38.629 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.060 | 0.060–0.060 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelSunFaces | 0.060 | 0.060–0.060 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 94.711 | 144.356 | 173.021 | 190.277 |

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
