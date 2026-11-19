| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.790 | 48.790–48.790 | n/a |
| frame p95 | 117.330 | 117.330–117.330 | n/a |
| frame p99 | 145.910 | 145.910–145.910 | n/a |
| steady frame avg | 41.560 | 41.560–41.560 | n/a |
| steady frame p95 | 52.750 | 52.750–52.750 | n/a |
| steady frame p99 | 73.600 | 73.600–73.600 | n/a |
| frame split INPUT | 0.109 | 0.109–0.109 | n/a |
| frame split UPDATE | 3.651 | 3.651–3.651 | n/a |
| frame split RENDER | 43.567 | 43.567–43.567 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.459 | 1.459–1.459 | n/a |
| GPU exclusive stage sum | 1.100 | 1.100–1.100 | n/a |
| GPU bakeSunShadowMap | 0.098 | 0.098–0.098 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.085 | 0.085–0.085 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.658 | 0.658–0.658 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.072 | 0.072–0.072 | n/a |
| input_to_present mean | 59.821 | 59.821–59.821 | n/a |
| input_to_present p99 | 80.108 | 80.108–80.108 | n/a |
| cursor_to_present mean | 58.535 | 58.535–58.535 | n/a |
| cursor_to_present p99 | 78.932 | 78.932–78.932 | n/a |
| frame_start_to_submit mean | 40.098 | 40.098–40.098 | n/a |
| frame_start_to_submit p99 | 73.566 | 73.566–73.566 | n/a |
| submit_to_present mean | 21.863 | 21.863–21.863 | n/a |
| submit_to_present p99 | 48.565 | 48.565–48.565 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.562 | 52.748 | 73.596 | 86.138 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.13 |

Render device counters: unsupported by this backend.
