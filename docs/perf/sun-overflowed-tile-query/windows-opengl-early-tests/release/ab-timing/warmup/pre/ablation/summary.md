| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.800 | 38.800–38.800 | n/a |
| frame p95 | 41.200 | 41.200–41.200 | n/a |
| frame p99 | 75.740 | 75.740–75.740 | n/a |
| steady frame avg | 38.010 | 38.010–38.010 | n/a |
| steady frame p95 | 40.530 | 40.530–40.530 | n/a |
| steady frame p99 | 41.640 | 41.640–41.640 | n/a |
| frame split INPUT | 0.180 | 0.180–0.180 | n/a |
| frame split UPDATE | 2.949 | 2.949–2.949 | n/a |
| frame split RENDER | 15.647 | 15.647–15.647 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.027 | 20.027–20.027 | n/a |
| GPU exclusive stage sum | 0.752 | 0.752–0.752 | n/a |
| GPU bakeSunShadowMap | 0.101 | 0.101–0.101 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.087 | 0.087–0.087 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 | n/a |
| GPU entityCanvasToFb | 0.288 | 0.288–0.288 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.061 | 0.061–0.061 | n/a |
| input_to_present mean | 74.082 | 74.082–74.082 | n/a |
| input_to_present p99 | 80.084 | 80.084–80.084 | n/a |
| cursor_to_present mean | 72.818 | 72.818–72.818 | n/a |
| cursor_to_present p99 | 78.886 | 78.886–78.886 | n/a |
| frame_start_to_submit mean | 16.836 | 16.836–16.836 | n/a |
| frame_start_to_submit p99 | 25.316 | 25.316–25.316 | n/a |
| submit_to_present mean | 59.102 | 59.102–59.102 | n/a |
| submit_to_present p99 | 67.176 | 67.176–67.176 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.015 | 40.528 | 41.638 | 42.182 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.30 | 2.30–2.30 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.4476 |

Render device counters: unsupported by this backend.
