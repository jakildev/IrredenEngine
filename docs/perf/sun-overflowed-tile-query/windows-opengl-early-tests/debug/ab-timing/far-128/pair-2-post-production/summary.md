| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.790 | 48.790–48.790 | n/a |
| frame p95 | 119.680 | 119.680–119.680 | n/a |
| frame p99 | 136.160 | 136.160–136.160 | n/a |
| steady frame avg | 41.350 | 41.350–41.350 | n/a |
| steady frame p95 | 44.780 | 44.780–44.780 | n/a |
| steady frame p99 | 50.570 | 50.570–50.570 | n/a |
| frame split INPUT | 0.181 | 0.181–0.181 | n/a |
| frame split UPDATE | 3.549 | 3.549–3.549 | n/a |
| frame split RENDER | 40.325 | 40.325–40.325 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.739 | 4.739–4.739 | n/a |
| GPU exclusive stage sum | 3.828 | 3.828–3.828 | n/a |
| GPU bakeSunShadowMap | 0.106 | 0.106–0.106 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.131 | 0.131–0.131 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 3.328 | 3.328–3.328 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |
| input_to_present mean | 79.418 | 79.418–79.418 | n/a |
| input_to_present p99 | 84.733 | 84.733–84.733 | n/a |
| cursor_to_present mean | 78.196 | 78.196–78.196 | n/a |
| cursor_to_present p99 | 83.544 | 83.544–83.544 | n/a |
| frame_start_to_submit mean | 36.313 | 36.313–36.313 | n/a |
| frame_start_to_submit p99 | 50.479 | 50.479–50.479 | n/a |
| submit_to_present mean | 45.157 | 45.157–45.157 | n/a |
| submit_to_present p99 | 54.495 | 54.495–54.495 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.353 | 44.776 | 50.567 | 54.368 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1008 |

Render device counters: unsupported by this backend.
