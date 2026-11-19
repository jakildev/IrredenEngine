| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.660 | 39.660–39.660 | n/a |
| frame p95 | 41.870 | 41.870–41.870 | n/a |
| frame p99 | 71.210 | 71.210–71.210 | n/a |
| steady frame avg | 38.690 | 38.690–38.690 | n/a |
| steady frame p95 | 41.000 | 41.000–41.000 | n/a |
| steady frame p99 | 42.050 | 42.050–42.050 | n/a |
| frame split INPUT | 0.208 | 0.208–0.208 | n/a |
| frame split UPDATE | 3.051 | 3.051–3.051 | n/a |
| frame split RENDER | 16.628 | 16.628–16.628 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 19.775 | 19.775–19.775 | n/a |
| GPU exclusive stage sum | 0.783 | 0.783–0.783 | n/a |
| GPU bakeSunShadowMap | 0.091 | 0.091–0.091 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.077 | 0.077–0.077 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.021 | 0.021–0.021 | n/a |
| GPU entityCanvasToFb | 0.360 | 0.360–0.360 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.063 | 0.063–0.063 | n/a |
| input_to_present mean | 75.324 | 75.324–75.324 | n/a |
| input_to_present p99 | 80.463 | 80.463–80.463 | n/a |
| cursor_to_present mean | 74.074 | 74.074–74.074 | n/a |
| cursor_to_present p99 | 79.140 | 79.140–79.140 | n/a |
| frame_start_to_submit mean | 17.354 | 17.354–17.354 | n/a |
| frame_start_to_submit p99 | 24.476 | 24.476–24.476 | n/a |
| submit_to_present mean | 59.940 | 59.940–59.940 | n/a |
| submit_to_present p99 | 65.562 | 65.562–65.562 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.690 | 40.996 | 42.051 | 42.549 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.40 | 2.40–2.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0911 |

Render device counters: unsupported by this backend.
