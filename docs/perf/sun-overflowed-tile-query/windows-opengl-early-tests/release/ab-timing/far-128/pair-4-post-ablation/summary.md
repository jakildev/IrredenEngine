| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.460 | 39.460–39.460 | n/a |
| frame p95 | 41.150 | 41.150–41.150 | n/a |
| frame p99 | 71.530 | 71.530–71.530 | n/a |
| steady frame avg | 38.590 | 38.590–38.590 | n/a |
| steady frame p95 | 40.860 | 40.860–40.860 | n/a |
| steady frame p99 | 41.440 | 41.440–41.440 | n/a |
| frame split INPUT | 0.397 | 0.397–0.397 | n/a |
| frame split UPDATE | 2.935 | 2.935–2.935 | n/a |
| frame split RENDER | 15.549 | 15.549–15.549 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.575 | 20.575–20.575 | n/a |
| GPU exclusive stage sum | 0.846 | 0.846–0.846 | n/a |
| GPU bakeSunShadowMap | 0.103 | 0.103–0.103 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.097 | 0.097–0.097 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU entityCanvasToFb | 0.376 | 0.376–0.376 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.064 | 0.064–0.064 | n/a |
| input_to_present mean | 75.231 | 75.231–75.231 | n/a |
| input_to_present p99 | 79.533 | 79.533–79.533 | n/a |
| cursor_to_present mean | 74.002 | 74.002–74.002 | n/a |
| cursor_to_present p99 | 78.496 | 78.496–78.496 | n/a |
| frame_start_to_submit mean | 16.542 | 16.542–16.542 | n/a |
| frame_start_to_submit p99 | 24.160 | 24.160–24.160 | n/a |
| submit_to_present mean | 60.551 | 60.551–60.551 | n/a |
| submit_to_present p99 | 67.182 | 67.182–67.182 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.588 | 40.859 | 41.436 | 41.762 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.40 | 2.40–2.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0949 |

Render device counters: unsupported by this backend.
