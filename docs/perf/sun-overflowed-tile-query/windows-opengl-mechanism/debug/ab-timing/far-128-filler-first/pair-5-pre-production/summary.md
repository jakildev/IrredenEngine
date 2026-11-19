| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 140.270 | 140.270–140.270 | n/a |
| frame p95 | 217.830 | 217.830–217.830 | n/a |
| frame p99 | 268.430 | 268.430–268.430 | n/a |
| steady frame avg | 130.590 | 130.590–130.590 | n/a |
| steady frame p95 | 168.740 | 168.740–168.740 | n/a |
| steady frame p99 | 187.450 | 187.450–187.450 | n/a |
| frame split INPUT | 0.308 | 0.308–0.308 | n/a |
| frame split UPDATE | 90.031 | 90.031–90.031 | n/a |
| frame split RENDER | 45.551 | 45.551–45.551 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.379 | 4.379–4.379 | n/a |
| GPU exclusive stage sum | 47.671 | 47.671–47.671 | n/a |
| GPU bakeSunShadowMap | 0.129 | 0.129–0.129 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.120 | 0.120–0.120 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 47.103 | 47.103–47.103 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.052 | 0.052–0.052 | n/a |
| GPU voxelStage2 | 0.017 | 0.017–0.017 | n/a |
| GPU voxelSunFaces | 0.058 | 0.058–0.058 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 130.588 | 168.742 | 187.455 | 199.633 |

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
