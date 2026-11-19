| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.080 | 39.080–39.080 | n/a |
| frame p95 | 41.240 | 41.240–41.240 | n/a |
| frame p99 | 74.370 | 74.370–74.370 | n/a |
| steady frame avg | 38.170 | 38.170–38.170 | n/a |
| steady frame p95 | 40.720 | 40.720–40.720 | n/a |
| steady frame p99 | 41.840 | 41.840–41.840 | n/a |
| frame split INPUT | 0.423 | 0.423–0.423 | n/a |
| frame split UPDATE | 2.888 | 2.888–2.888 | n/a |
| frame split RENDER | 15.468 | 15.468–15.468 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.301 | 20.301–20.301 | n/a |
| GPU exclusive stage sum | 0.815 | 0.815–0.815 | n/a |
| GPU bakeSunShadowMap | 0.096 | 0.096–0.096 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.092 | 0.092–0.092 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.024 | 0.024–0.024 | n/a |
| GPU entityCanvasToFb | 0.362 | 0.362–0.362 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.063 | 0.063–0.063 | n/a |
| input_to_present mean | 74.497 | 74.497–74.497 | n/a |
| input_to_present p99 | 80.998 | 80.998–80.998 | n/a |
| cursor_to_present mean | 73.314 | 73.314–73.314 | n/a |
| cursor_to_present p99 | 79.956 | 79.956–79.956 | n/a |
| frame_start_to_submit mean | 16.162 | 16.162–16.162 | n/a |
| frame_start_to_submit p99 | 23.124 | 23.124–23.124 | n/a |
| submit_to_present mean | 60.108 | 60.108–60.108 | n/a |
| submit_to_present p99 | 66.727 | 66.727–66.727 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.173 | 40.725 | 41.843 | 42.220 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.30 | 2.30–2.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1234 |

Render device counters: unsupported by this backend.
