| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 47.630 | 47.630–47.630 | n/a |
| frame p95 | 116.900 | 116.900–116.900 | n/a |
| frame p99 | 138.620 | 138.620–138.620 | n/a |
| steady frame avg | 39.650 | 39.650–39.650 | n/a |
| steady frame p95 | 49.020 | 49.020–49.020 | n/a |
| steady frame p99 | 69.860 | 69.860–69.860 | n/a |
| frame split INPUT | 0.135 | 0.135–0.135 | n/a |
| frame split UPDATE | 3.594 | 3.594–3.594 | n/a |
| frame split RENDER | 42.321 | 42.321–42.321 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.579 | 1.579–1.579 | n/a |
| GPU exclusive stage sum | 1.100 | 1.100–1.100 | n/a |
| GPU bakeSunShadowMap | 0.099 | 0.099–0.099 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.096 | 0.096–0.096 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 0.651 | 0.651–0.651 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.021 | 0.021–0.021 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.073 | 0.073–0.073 | n/a |
| input_to_present mean | 66.453 | 66.453–66.453 | n/a |
| input_to_present p99 | 79.884 | 79.884–79.884 | n/a |
| cursor_to_present mean | 65.187 | 65.187–65.187 | n/a |
| cursor_to_present p99 | 78.624 | 78.624–78.624 | n/a |
| frame_start_to_submit mean | 37.886 | 37.886–37.886 | n/a |
| frame_start_to_submit p99 | 69.821 | 69.821–69.821 | n/a |
| submit_to_present mean | 30.537 | 30.537–30.537 | n/a |
| submit_to_present p99 | 48.646 | 48.646–48.646 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 39.651 | 49.016 | 69.855 | 73.056 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0877 |

Render device counters: unsupported by this backend.
