| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 106.200 | 106.200–106.200 | n/a |
| frame p95 | 191.720 | 191.720–191.720 | n/a |
| frame p99 | 244.240 | 244.240–244.240 | n/a |
| steady frame avg | 95.030 | 95.030–95.030 | n/a |
| steady frame p95 | 139.150 | 139.150–139.150 | n/a |
| steady frame p99 | 184.730 | 184.730–184.730 | n/a |
| frame split INPUT | 1.430 | 1.430–1.430 | n/a |
| frame split UPDATE | 63.066 | 63.066–63.066 | n/a |
| frame split RENDER | 40.387 | 40.387–40.387 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.314 | 1.314–1.314 | n/a |
| GPU exclusive stage sum | 51.481 | 51.481–51.481 | n/a |
| GPU bakeSunShadowMap | 0.127 | 0.127–0.127 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 50.954 | 50.954–50.954 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.056 | 0.056–0.056 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 95.030 | 139.151 | 184.728 | 256.150 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.90 | 5.90–5.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
