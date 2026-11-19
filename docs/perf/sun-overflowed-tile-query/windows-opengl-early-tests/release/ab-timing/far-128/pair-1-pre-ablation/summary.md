| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.600 | 39.600–39.600 | n/a |
| frame p95 | 41.710 | 41.710–41.710 | n/a |
| frame p99 | 78.380 | 78.380–78.380 | n/a |
| steady frame avg | 38.740 | 38.740–38.740 | n/a |
| steady frame p95 | 41.440 | 41.440–41.440 | n/a |
| steady frame p99 | 42.310 | 42.310–42.310 | n/a |
| frame split INPUT | 0.377 | 0.377–0.377 | n/a |
| frame split UPDATE | 3.037 | 3.037–3.037 | n/a |
| frame split RENDER | 16.069 | 16.069–16.069 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 20.113 | 20.113–20.113 | n/a |
| GPU exclusive stage sum | 0.743 | 0.743–0.743 | n/a |
| GPU bakeSunShadowMap | 0.090 | 0.090–0.090 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.093 | 0.093–0.093 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.022 | 0.022–0.022 | n/a |
| GPU entityCanvasToFb | 0.289 | 0.289–0.289 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.064 | 0.064–0.064 | n/a |
| input_to_present mean | 75.440 | 75.440–75.440 | n/a |
| input_to_present p99 | 81.835 | 81.835–81.835 | n/a |
| cursor_to_present mean | 74.197 | 74.197–74.197 | n/a |
| cursor_to_present p99 | 80.806 | 80.806–80.806 | n/a |
| frame_start_to_submit mean | 17.289 | 17.289–17.289 | n/a |
| frame_start_to_submit p99 | 23.429 | 23.429–23.429 | n/a |
| submit_to_present mean | 60.100 | 60.100–60.100 | n/a |
| submit_to_present p99 | 69.518 | 69.518–69.518 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.738 | 41.441 | 42.314 | 42.864 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.40 | 2.40–2.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0801 |

Render device counters: unsupported by this backend.
