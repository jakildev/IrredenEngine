| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 119.270 | 119.270–119.270 | n/a |
| frame p95 | 192.250 | 192.250–192.250 | n/a |
| frame p99 | 225.060 | 225.060–225.060 | n/a |
| steady frame avg | 109.970 | 109.970–109.970 | n/a |
| steady frame p95 | 141.940 | 141.940–141.940 | n/a |
| steady frame p99 | 165.050 | 165.050–165.050 | n/a |
| frame split INPUT | 0.208 | 0.208–0.208 | n/a |
| frame split UPDATE | 76.784 | 76.784–76.784 | n/a |
| frame split RENDER | 40.829 | 40.829–40.829 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.450 | 1.450–1.450 | n/a |
| GPU exclusive stage sum | 1.802 | 1.802–1.802 | n/a |
| GPU bakeSunShadowMap | 0.146 | 0.146–0.146 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.089 | 0.089–0.089 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.029 | 0.029–0.029 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.178 | 1.178–1.178 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.039 | 0.039–0.039 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.082 | 0.082–0.082 | n/a |
| GPU voxelStage2 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 109.971 | 141.944 | 165.049 | 174.189 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.70 | 6.70–6.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
