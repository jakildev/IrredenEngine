| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 41.930 | 41.930–41.930 | n/a |
| frame p95 | 44.920 | 44.920–44.920 | n/a |
| frame p99 | 72.700 | 72.700–72.700 | n/a |
| steady frame avg | 41.300 | 41.300–41.300 | n/a |
| steady frame p95 | 43.550 | 43.550–43.550 | n/a |
| steady frame p99 | 45.370 | 45.370–45.370 | n/a |
| frame split INPUT | 0.085 | 0.085–0.085 | n/a |
| frame split UPDATE | 3.007 | 3.007–3.007 | n/a |
| frame split RENDER | 15.475 | 15.475–15.475 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 23.359 | 23.359–23.359 | n/a |
| GPU exclusive stage sum | 3.777 | 3.777–3.777 | n/a |
| GPU bakeSunShadowMap | 0.092 | 0.092–0.092 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.145 | 0.145–0.145 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.026 | 0.026–0.026 | n/a |
| GPU entityCanvasToFb | 3.259 | 3.259–3.259 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.064 | 0.064–0.064 | n/a |
| input_to_present mean | 80.518 | 80.518–80.518 | n/a |
| input_to_present p99 | 85.922 | 85.922–85.922 | n/a |
| cursor_to_present mean | 79.325 | 79.325–79.325 | n/a |
| cursor_to_present p99 | 84.720 | 84.720–84.720 | n/a |
| frame_start_to_submit mean | 15.890 | 15.890–15.890 | n/a |
| frame_start_to_submit p99 | 23.245 | 23.245–23.245 | n/a |
| submit_to_present mean | 66.635 | 66.635–66.635 | n/a |
| submit_to_present p99 | 72.404 | 72.404–72.404 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.301 | 43.547 | 45.369 | 45.858 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.50 | 2.50–2.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1102 |

Render device counters: unsupported by this backend.
