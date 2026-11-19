| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 116.430 | 116.430–116.430 | n/a |
| frame p95 | 198.480 | 198.480–198.480 | n/a |
| frame p99 | 237.820 | 237.820–237.820 | n/a |
| steady frame avg | 109.520 | 109.520–109.520 | n/a |
| steady frame p95 | 154.330 | 154.330–154.330 | n/a |
| steady frame p99 | 171.890 | 171.890–171.890 | n/a |
| frame split INPUT | 0.183 | 0.183–0.183 | n/a |
| frame split UPDATE | 73.488 | 73.488–73.488 | n/a |
| frame split RENDER | 41.689 | 41.689–41.689 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.069 | 1.069–1.069 | n/a |
| GPU exclusive stage sum | 1.876 | 1.876–1.876 | n/a |
| GPU bakeSunShadowMap | 0.138 | 0.138–0.138 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.051 | 0.051–0.051 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.230 | 1.230–1.230 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.072 | 0.072–0.072 | n/a |
| GPU voxelStage2 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelSunFaces | 0.070 | 0.070–0.070 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 109.520 | 154.326 | 171.887 | 185.635 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.40 | 6.40–6.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
