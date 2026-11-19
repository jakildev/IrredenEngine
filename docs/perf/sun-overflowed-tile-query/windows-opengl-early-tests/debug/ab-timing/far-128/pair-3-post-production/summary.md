| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.990 | 48.990–48.990 | n/a |
| frame p95 | 117.990 | 117.990–117.990 | n/a |
| frame p99 | 141.380 | 141.380–141.380 | n/a |
| steady frame avg | 41.300 | 41.300–41.300 | n/a |
| steady frame p95 | 44.240 | 44.240–44.240 | n/a |
| steady frame p99 | 50.580 | 50.580–50.580 | n/a |
| frame split INPUT | 0.128 | 0.128–0.128 | n/a |
| frame split UPDATE | 3.624 | 3.624–3.624 | n/a |
| frame split RENDER | 41.243 | 41.243–41.243 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 3.991 | 3.991–3.991 | n/a |
| GPU exclusive stage sum | 3.922 | 3.922–3.922 | n/a |
| GPU bakeSunShadowMap | 0.096 | 0.096–0.096 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.135 | 0.135–0.135 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 3.431 | 3.431–3.431 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |
| input_to_present mean | 79.523 | 79.523–79.523 | n/a |
| input_to_present p99 | 83.994 | 83.994–83.994 | n/a |
| cursor_to_present mean | 78.264 | 78.264–78.264 | n/a |
| cursor_to_present p99 | 82.714 | 82.714–82.714 | n/a |
| frame_start_to_submit mean | 36.860 | 36.860–36.860 | n/a |
| frame_start_to_submit p99 | 50.496 | 50.496–50.496 | n/a |
| submit_to_present mean | 44.760 | 44.760–44.760 | n/a |
| submit_to_present p99 | 54.355 | 54.355–54.355 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 41.301 | 44.244 | 50.581 | 53.347 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0938 |

Render device counters: unsupported by this backend.
