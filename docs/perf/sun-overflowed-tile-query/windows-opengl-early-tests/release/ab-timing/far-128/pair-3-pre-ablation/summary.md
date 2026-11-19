| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.390 | 39.390–39.390 | n/a |
| frame p95 | 41.120 | 41.120–41.120 | n/a |
| frame p99 | 66.300 | 66.300–66.300 | n/a |
| steady frame avg | 38.560 | 38.560–38.560 | n/a |
| steady frame p95 | 40.880 | 40.880–40.880 | n/a |
| steady frame p99 | 41.950 | 41.950–41.950 | n/a |
| frame split INPUT | 0.343 | 0.343–0.343 | n/a |
| frame split UPDATE | 2.906 | 2.906–2.906 | n/a |
| frame split RENDER | 15.507 | 15.507–15.507 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.629 | 20.629–20.629 | n/a |
| GPU exclusive stage sum | 0.756 | 0.756–0.756 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.035 | 0.035–0.035 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.030 | 0.030–0.030 | n/a |
| GPU entityCanvasToFb | 0.297 | 0.297–0.297 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.069 | 0.069–0.069 | n/a |
| input_to_present mean | 75.185 | 75.185–75.185 | n/a |
| input_to_present p99 | 80.804 | 80.804–80.804 | n/a |
| cursor_to_present mean | 73.942 | 73.942–73.942 | n/a |
| cursor_to_present p99 | 79.486 | 79.486–79.486 | n/a |
| frame_start_to_submit mean | 16.587 | 16.587–16.587 | n/a |
| frame_start_to_submit p99 | 24.389 | 24.389–24.389 | n/a |
| submit_to_present mean | 60.466 | 60.466–60.466 | n/a |
| submit_to_present p99 | 66.012 | 66.012–66.012 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.560 | 40.875 | 41.953 | 42.649 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.40 | 2.40–2.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0853 |

Render device counters: unsupported by this backend.
