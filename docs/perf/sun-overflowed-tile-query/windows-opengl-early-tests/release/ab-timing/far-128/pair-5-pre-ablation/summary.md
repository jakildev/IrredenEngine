| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.440 | 32.440–32.440 | n/a |
| frame p95 | 33.540 | 33.540–33.540 | n/a |
| frame p99 | 81.750 | 81.750–81.750 | n/a |
| steady frame avg | 31.020 | 31.020–31.020 | n/a |
| steady frame p95 | 31.920 | 31.920–31.920 | n/a |
| steady frame p99 | 32.280 | 32.280–32.280 | n/a |
| frame split INPUT | 0.367 | 0.367–0.367 | n/a |
| frame split UPDATE | 2.326 | 2.326–2.326 | n/a |
| frame split RENDER | 14.495 | 14.495–14.495 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 15.256 | 15.256–15.256 | n/a |
| GPU exclusive stage sum | 0.654 | 0.654–0.654 | n/a |
| GPU bakeSunShadowMap | 0.084 | 0.084–0.084 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.077 | 0.077–0.077 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.019 | 0.019–0.019 | n/a |
| GPU entityCanvasToFb | 0.254 | 0.254–0.254 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.024 | 0.024–0.024 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.021 | 0.021–0.021 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.058 | 0.058–0.058 | n/a |
| input_to_present mean | 60.719 | 60.719–60.719 | n/a |
| input_to_present p99 | 63.433 | 63.433–63.433 | n/a |
| cursor_to_present mean | 59.483 | 59.483–59.483 | n/a |
| cursor_to_present p99 | 62.301 | 62.301–62.301 | n/a |
| frame_start_to_submit mean | 14.814 | 14.814–14.814 | n/a |
| frame_start_to_submit p99 | 21.372 | 21.372–21.372 | n/a |
| submit_to_present mean | 47.138 | 47.138–47.138 | n/a |
| submit_to_present p99 | 51.278 | 51.278–51.278 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 31.016 | 31.916 | 32.285 | 33.539 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.90 | 1.90–1.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0821 |

Render device counters: unsupported by this backend.
