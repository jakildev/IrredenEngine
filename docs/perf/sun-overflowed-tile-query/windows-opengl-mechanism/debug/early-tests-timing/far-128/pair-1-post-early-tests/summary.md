| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 40.770 | 40.770–40.770 | n/a |
| frame p95 | 102.460 | 102.460–102.460 | n/a |
| frame p99 | 118.910 | 118.910–118.910 | n/a |
| steady frame avg | 33.730 | 33.730–33.730 | n/a |
| steady frame p95 | 41.220 | 41.220–41.220 | n/a |
| steady frame p99 | 49.840 | 49.840–49.840 | n/a |
| frame split INPUT | 0.338 | 0.338–0.338 | n/a |
| frame split UPDATE | 2.691 | 2.691–2.691 | n/a |
| frame split RENDER | 36.739 | 36.739–36.739 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.007 | 1.007–1.007 | n/a |
| GPU exclusive stage sum | 3.227 | 3.227–3.227 | n/a |
| GPU bakeSunShadowMap | 0.082 | 0.082–0.082 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 2.763 | 2.763–2.763 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.019 | 0.019–0.019 | n/a |
| GPU voxelStage2 | 0.009 | 0.009–0.009 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 33.731 | 41.223 | 49.839 | 80.997 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.40 | 2.40–2.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
