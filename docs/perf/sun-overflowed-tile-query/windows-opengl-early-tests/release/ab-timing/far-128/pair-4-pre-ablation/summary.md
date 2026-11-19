| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.150 | 39.150–39.150 | n/a |
| frame p95 | 40.990 | 40.990–40.990 | n/a |
| frame p99 | 68.070 | 68.070–68.070 | n/a |
| steady frame avg | 38.310 | 38.310–38.310 | n/a |
| steady frame p95 | 40.570 | 40.570–40.570 | n/a |
| steady frame p99 | 42.270 | 42.270–42.270 | n/a |
| frame split INPUT | 0.346 | 0.346–0.346 | n/a |
| frame split UPDATE | 2.922 | 2.922–2.922 | n/a |
| frame split RENDER | 15.675 | 15.675–15.675 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.204 | 20.204–20.204 | n/a |
| GPU exclusive stage sum | 0.753 | 0.753–0.753 | n/a |
| GPU bakeSunShadowMap | 0.102 | 0.102–0.102 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.088 | 0.088–0.088 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.034 | 0.034–0.034 | n/a |
| GPU entityCanvasToFb | 0.287 | 0.287–0.287 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.063 | 0.063–0.063 | n/a |
| input_to_present mean | 74.625 | 74.625–74.625 | n/a |
| input_to_present p99 | 80.471 | 80.471–80.471 | n/a |
| cursor_to_present mean | 73.374 | 73.374–73.374 | n/a |
| cursor_to_present p99 | 78.651 | 78.651–78.651 | n/a |
| frame_start_to_submit mean | 16.765 | 16.765–16.765 | n/a |
| frame_start_to_submit p99 | 25.969 | 25.969–25.969 | n/a |
| submit_to_present mean | 59.751 | 59.751–59.751 | n/a |
| submit_to_present p99 | 66.485 | 66.485–66.485 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.313 | 40.570 | 42.273 | 44.078 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.30 | 2.30–2.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1159 |

Render device counters: unsupported by this backend.
