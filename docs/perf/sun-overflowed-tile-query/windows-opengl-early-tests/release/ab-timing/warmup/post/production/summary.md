| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 41.890 | 41.890–41.890 | n/a |
| frame p95 | 44.420 | 44.420–44.420 | n/a |
| frame p99 | 67.810 | 67.810–67.810 | n/a |
| steady frame avg | 41.330 | 41.330–41.330 | n/a |
| steady frame p95 | 43.370 | 43.370–43.370 | n/a |
| steady frame p99 | 44.770 | 44.770–44.770 | n/a |
| frame split INPUT | 0.162 | 0.162–0.162 | n/a |
| frame split UPDATE | 3.101 | 3.101–3.101 | n/a |
| frame split RENDER | 15.721 | 15.721–15.721 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 22.906 | 22.906–22.906 | n/a |
| GPU exclusive stage sum | 3.862 | 3.862–3.862 | n/a |
| GPU bakeSunShadowMap | 0.100 | 0.100–0.100 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.028 | 0.028–0.028 | n/a |
| GPU entityCanvasToFb | 3.360 | 3.360–3.360 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.038 | 0.038–0.038 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.062 | 0.062–0.062 | n/a |
| input_to_present mean | 80.586 | 80.586–80.586 | n/a |
| input_to_present p99 | 87.366 | 87.366–87.366 | n/a |
| cursor_to_present mean | 79.393 | 79.393–79.393 | n/a |
| cursor_to_present p99 | 85.764 | 85.764–85.764 | n/a |
| frame_start_to_submit mean | 16.265 | 16.265–16.265 | n/a |
| frame_start_to_submit p99 | 24.458 | 24.458–24.458 | n/a |
| submit_to_present mean | 66.308 | 66.308–66.308 | n/a |
| submit_to_present p99 | 71.872 | 71.872–71.872 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.331 | 43.366 | 44.774 | 46.801 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.50 | 2.50–2.50 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.261 |

Render device counters: unsupported by this backend.
