| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 45.250 | 45.250–45.250 | n/a |
| frame p95 | 108.540 | 108.540–108.540 | n/a |
| frame p99 | 133.190 | 133.190–133.190 | n/a |
| steady frame avg | 38.560 | 38.560–38.560 | n/a |
| steady frame p95 | 47.250 | 47.250–47.250 | n/a |
| steady frame p99 | 54.460 | 54.460–54.460 | n/a |
| frame split INPUT | 0.092 | 0.092–0.092 | n/a |
| frame split UPDATE | 3.237 | 3.237–3.237 | n/a |
| frame split RENDER | 40.259 | 40.259–40.259 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.661 | 1.661–1.661 | n/a |
| GPU exclusive stage sum | 1.001 | 1.001–1.001 | n/a |
| GPU bakeSunShadowMap | 0.093 | 0.093–0.093 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.557 | 0.557–0.557 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.066 | 0.066–0.066 | n/a |
| input_to_present mean | 63.467 | 63.467–63.467 | n/a |
| input_to_present p99 | 78.837 | 78.837–78.837 | n/a |
| cursor_to_present mean | 62.234 | 62.234–62.234 | n/a |
| cursor_to_present p99 | 77.604 | 77.604–77.604 | n/a |
| frame_start_to_submit mean | 36.819 | 36.819–36.819 | n/a |
| frame_start_to_submit p99 | 54.433 | 54.433–54.433 | n/a |
| submit_to_present mean | 28.414 | 28.414–28.414 | n/a |
| submit_to_present p99 | 48.388 | 48.388–48.388 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.559 | 47.253 | 54.464 | 57.656 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.70 | 2.70–2.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.7018 |

Render device counters: unsupported by this backend.
