| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 101.270 | 101.270–101.270 | n/a |
| frame p95 | 112.870 | 112.870–112.870 | n/a |
| frame p99 | 175.310 | 175.310–175.310 | n/a |
| steady frame avg | 99.220 | 99.220–99.220 | n/a |
| steady frame p95 | 112.270 | 112.270–112.270 | n/a |
| steady frame p99 | 115.750 | 115.750–115.750 | n/a |
| frame split INPUT | 0.577 | 0.577–0.577 | n/a |
| frame split UPDATE | 7.227 | 7.227–7.227 | n/a |
| frame split RENDER | 38.812 | 38.812–38.812 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 54.651 | 54.651–54.651 | n/a |
| GPU exclusive stage sum | 62.833 | 62.833–62.833 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.016 | 0.016–0.016 | n/a |
| GPU entityCanvasToFb | 62.348 | 62.348–62.348 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.060 | 0.060–0.060 | n/a |
| input_to_present mean | 191.909 | 191.909–191.909 | n/a |
| input_to_present p99 | 217.261 | 217.261–217.261 | n/a |
| cursor_to_present mean | 190.723 | 190.723–190.723 | n/a |
| cursor_to_present p99 | 216.149 | 216.149–216.149 | n/a |
| frame_start_to_submit mean | 40.855 | 40.855–40.855 | n/a |
| frame_start_to_submit p99 | 57.200 | 57.200–57.200 | n/a |
| submit_to_present mean | 157.393 | 157.393–157.393 | n/a |
| submit_to_present p99 | 185.475 | 185.475–185.475 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 99.216 | 112.270 | 115.746 | 128.764 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.00 | 6.00–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.2408 |

Render device counters: unsupported by this backend.
