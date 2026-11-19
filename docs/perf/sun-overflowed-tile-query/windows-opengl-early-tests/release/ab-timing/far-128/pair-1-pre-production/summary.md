| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 101.770 | 101.770–101.770 | n/a |
| frame p95 | 111.550 | 111.550–111.550 | n/a |
| frame p99 | 121.480 | 121.480–121.480 | n/a |
| steady frame avg | 100.190 | 100.190–100.190 | n/a |
| steady frame p95 | 109.370 | 109.370–109.370 | n/a |
| steady frame p99 | 115.740 | 115.740–115.740 | n/a |
| frame split INPUT | 0.649 | 0.649–0.649 | n/a |
| frame split UPDATE | 6.998 | 6.998–6.998 | n/a |
| frame split RENDER | 15.723 | 15.723–15.723 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 78.403 | 78.403–78.403 | n/a |
| GPU exclusive stage sum | 63.783 | 63.783–63.783 | n/a |
| GPU bakeSunShadowMap | 0.100 | 0.100–0.100 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.136 | 0.136–0.136 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU entityCanvasToFb | 63.267 | 63.267–63.267 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.061 | 0.061–0.061 | n/a |
| input_to_present mean | 194.150 | 194.150–194.150 | n/a |
| input_to_present p99 | 216.987 | 216.987–216.987 | n/a |
| cursor_to_present mean | 193.039 | 193.039–193.039 | n/a |
| cursor_to_present p99 | 215.963 | 215.963–215.963 | n/a |
| frame_start_to_submit mean | 20.785 | 20.785–20.785 | n/a |
| frame_start_to_submit p99 | 29.073 | 29.073–29.073 | n/a |
| submit_to_present mean | 179.467 | 179.467–179.467 | n/a |
| submit_to_present p99 | 202.674 | 202.674–202.674 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 100.192 | 109.365 | 115.740 | 120.620 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.00 | 6.00–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1145 |

Render device counters: unsupported by this backend.
