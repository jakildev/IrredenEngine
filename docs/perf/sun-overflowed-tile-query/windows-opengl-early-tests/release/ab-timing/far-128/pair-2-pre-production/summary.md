| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 101.980 | 101.980–101.980 | n/a |
| frame p95 | 112.280 | 112.280–112.280 | n/a |
| frame p99 | 115.840 | 115.840–115.840 | n/a |
| steady frame avg | 101.810 | 101.810–101.810 | n/a |
| steady frame p95 | 111.650 | 111.650–111.650 | n/a |
| steady frame p99 | 114.900 | 114.900–114.900 | n/a |
| frame split INPUT | 0.569 | 0.569–0.569 | n/a |
| frame split UPDATE | 7.089 | 7.089–7.089 | n/a |
| frame split RENDER | 15.240 | 15.240–15.240 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 79.081 | 79.081–79.081 | n/a |
| GPU exclusive stage sum | 64.063 | 64.063–64.063 | n/a |
| GPU bakeSunShadowMap | 0.093 | 0.093–0.093 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.135 | 0.135–0.135 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU entityCanvasToFb | 63.569 | 63.569–63.569 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.056 | 0.056–0.056 | n/a |
| input_to_present mean | 197.175 | 197.175–197.175 | n/a |
| input_to_present p99 | 216.507 | 216.507–216.507 | n/a |
| cursor_to_present mean | 196.015 | 196.015–196.015 | n/a |
| cursor_to_present p99 | 215.481 | 215.481–215.481 | n/a |
| frame_start_to_submit mean | 20.582 | 20.582–20.582 | n/a |
| frame_start_to_submit p99 | 28.923 | 28.923–28.923 | n/a |
| submit_to_present mean | 182.985 | 182.985–182.985 | n/a |
| submit_to_present p99 | 200.870 | 200.870–200.870 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.811 | 111.655 | 114.902 | 115.843 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0917 |

Render device counters: unsupported by this backend.
