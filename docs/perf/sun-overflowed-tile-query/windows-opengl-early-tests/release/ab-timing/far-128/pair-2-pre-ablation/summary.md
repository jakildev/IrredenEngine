| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.270 | 39.270–39.270 | n/a |
| frame p95 | 41.710 | 41.710–41.710 | n/a |
| frame p99 | 53.980 | 53.980–53.980 | n/a |
| steady frame avg | 38.690 | 38.690–38.690 | n/a |
| steady frame p95 | 41.400 | 41.400–41.400 | n/a |
| steady frame p99 | 42.410 | 42.410–42.410 | n/a |
| frame split INPUT | 0.397 | 0.397–0.397 | n/a |
| frame split UPDATE | 2.996 | 2.996–2.996 | n/a |
| frame split RENDER | 16.127 | 16.127–16.127 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 19.747 | 19.747–19.747 | n/a |
| GPU exclusive stage sum | 0.754 | 0.754–0.754 | n/a |
| GPU bakeSunShadowMap | 0.092 | 0.092–0.092 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.090 | 0.090–0.090 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.032 | 0.032–0.032 | n/a |
| GPU entityCanvasToFb | 0.296 | 0.296–0.296 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.068 | 0.068–0.068 | n/a |
| input_to_present mean | 75.419 | 75.419–75.419 | n/a |
| input_to_present p99 | 80.994 | 80.994–80.994 | n/a |
| cursor_to_present mean | 74.189 | 74.189–74.189 | n/a |
| cursor_to_present p99 | 79.979 | 79.979–79.979 | n/a |
| frame_start_to_submit mean | 17.332 | 17.332–17.332 | n/a |
| frame_start_to_submit p99 | 25.869 | 25.869–25.869 | n/a |
| submit_to_present mean | 59.965 | 59.965–59.965 | n/a |
| submit_to_present p99 | 66.933 | 66.933–66.933 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.694 | 41.399 | 42.409 | 42.982 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.30 | 2.30–2.30 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.105 |

Render device counters: unsupported by this backend.
