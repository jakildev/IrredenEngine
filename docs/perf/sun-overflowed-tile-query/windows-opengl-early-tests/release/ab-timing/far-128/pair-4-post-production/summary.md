| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 42.240 | 42.240–42.240 | n/a |
| frame p95 | 45.110 | 45.110–45.110 | n/a |
| frame p99 | 69.760 | 69.760–69.760 | n/a |
| steady frame avg | 41.710 | 41.710–41.710 | n/a |
| steady frame p95 | 44.090 | 44.090–44.090 | n/a |
| steady frame p99 | 45.160 | 45.160–45.160 | n/a |
| frame split INPUT | 0.079 | 0.079–0.079 | n/a |
| frame split UPDATE | 3.126 | 3.126–3.126 | n/a |
| frame split RENDER | 16.045 | 16.045–16.045 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 22.986 | 22.986–22.986 | n/a |
| GPU exclusive stage sum | 3.867 | 3.867–3.867 | n/a |
| GPU bakeSunShadowMap | 0.092 | 0.092–0.092 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.135 | 0.135–0.135 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.020 | 0.020–0.020 | n/a |
| GPU entityCanvasToFb | 3.367 | 3.367–3.367 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.061 | 0.061–0.061 | n/a |
| input_to_present mean | 81.236 | 81.236–81.236 | n/a |
| input_to_present p99 | 86.204 | 86.204–86.204 | n/a |
| cursor_to_present mean | 80.019 | 80.019–80.019 | n/a |
| cursor_to_present p99 | 84.446 | 84.446–84.446 | n/a |
| frame_start_to_submit mean | 16.852 | 16.852–16.852 | n/a |
| frame_start_to_submit p99 | 24.018 | 24.018–24.018 | n/a |
| submit_to_present mean | 66.491 | 66.491–66.491 | n/a |
| submit_to_present p99 | 72.400 | 72.400–72.400 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.712 | 44.091 | 45.164 | 45.563 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.50 | 2.50–2.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.3687 |

Render device counters: unsupported by this backend.
