| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.430 | 102.430–102.430 | n/a |
| frame p95 | 186.680 | 186.680–186.680 | n/a |
| frame p99 | 214.010 | 214.010–214.010 | n/a |
| steady frame avg | 92.170 | 92.170–92.170 | n/a |
| steady frame p95 | 120.850 | 120.850–120.850 | n/a |
| steady frame p99 | 152.950 | 152.950–152.950 | n/a |
| frame split INPUT | 0.144 | 0.144–0.144 | n/a |
| frame split UPDATE | 61.302 | 61.302–61.302 | n/a |
| frame split RENDER | 40.381 | 40.381–40.381 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.600 | 0.600–0.600 | n/a |
| GPU exclusive stage sum | 1.462 | 1.462–1.462 | n/a |
| GPU bakeSunShadowMap | 0.127 | 0.127–0.127 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.028 | 0.028–0.028 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 0.952 | 0.952–0.952 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.033 | 0.033–0.033 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 92.170 | 120.848 | 152.954 | 158.743 |

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
