| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 47.020 | 47.020–47.020 | n/a |
| frame p95 | 123.950 | 123.950–123.950 | n/a |
| frame p99 | 154.550 | 154.550–154.550 | n/a |
| steady frame avg | 38.830 | 38.830–38.830 | n/a |
| steady frame p95 | 45.930 | 45.930–45.930 | n/a |
| steady frame p99 | 53.390 | 53.390–53.390 | n/a |
| frame split INPUT | 0.088 | 0.088–0.088 | n/a |
| frame split UPDATE | 3.473 | 3.473–3.473 | n/a |
| frame split RENDER | 41.347 | 41.347–41.347 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.109 | 2.109–2.109 | n/a |
| GPU exclusive stage sum | 1.150 | 1.150–1.150 | n/a |
| GPU bakeSunShadowMap | 0.091 | 0.091–0.091 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.092 | 0.092–0.092 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.011 | 0.011–0.011 | n/a |
| GPU entityCanvasToFb | 0.701 | 0.701–0.701 | n/a |
| GPU fbToScreen | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.029 | 0.029–0.029 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 | n/a |
| input_to_present mean | 68.274 | 68.274–68.274 | n/a |
| input_to_present p99 | 81.061 | 81.061–81.061 | n/a |
| cursor_to_present mean | 66.999 | 66.999–66.999 | n/a |
| cursor_to_present p99 | 79.979 | 79.979–79.979 | n/a |
| frame_start_to_submit mean | 36.632 | 36.632–36.632 | n/a |
| frame_start_to_submit p99 | 53.355 | 53.355–53.355 | n/a |
| submit_to_present mean | 33.542 | 33.542–33.542 | n/a |
| submit_to_present p99 | 48.633 | 48.633–48.633 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.827 | 45.926 | 53.391 | 59.230 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.4662 |

Render device counters: unsupported by this backend.
