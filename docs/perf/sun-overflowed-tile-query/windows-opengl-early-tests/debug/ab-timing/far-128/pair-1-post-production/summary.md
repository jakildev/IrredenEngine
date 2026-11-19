| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.900 | 48.900–48.900 | n/a |
| frame p95 | 114.770 | 114.770–114.770 | n/a |
| frame p99 | 130.900 | 130.900–130.900 | n/a |
| steady frame avg | 41.780 | 41.780–41.780 | n/a |
| steady frame p95 | 46.190 | 46.190–46.190 | n/a |
| steady frame p99 | 49.370 | 49.370–49.370 | n/a |
| frame split INPUT | 0.109 | 0.109–0.109 | n/a |
| frame split UPDATE | 3.681 | 3.681–3.681 | n/a |
| frame split RENDER | 41.103 | 41.103–41.103 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.011 | 4.011–4.011 | n/a |
| GPU exclusive stage sum | 3.805 | 3.805–3.805 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.144 | 0.144–0.144 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 3.311 | 3.311–3.311 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.028 | 0.028–0.028 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |
| input_to_present mean | 80.359 | 80.359–80.359 | n/a |
| input_to_present p99 | 89.702 | 89.702–89.702 | n/a |
| cursor_to_present mean | 79.086 | 79.086–79.086 | n/a |
| cursor_to_present p99 | 88.419 | 88.419–88.419 | n/a |
| frame_start_to_submit mean | 37.231 | 37.231–37.231 | n/a |
| frame_start_to_submit p99 | 49.341 | 49.341–49.341 | n/a |
| submit_to_present mean | 45.327 | 45.327–45.327 | n/a |
| submit_to_present p99 | 57.158 | 57.158–57.158 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.783 | 46.193 | 49.369 | 51.689 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0892 |

Render device counters: unsupported by this backend.
