| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.320 | 46.320–46.320 | n/a |
| frame p95 | 110.090 | 110.090–110.090 | n/a |
| frame p99 | 150.080 | 150.080–150.080 | n/a |
| steady frame avg | 38.910 | 38.910–38.910 | n/a |
| steady frame p95 | 47.510 | 47.510–47.510 | n/a |
| steady frame p99 | 56.500 | 56.500–56.500 | n/a |
| frame split INPUT | 0.097 | 0.097–0.097 | n/a |
| frame split UPDATE | 3.341 | 3.341–3.341 | n/a |
| frame split RENDER | 41.221 | 41.221–41.221 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.662 | 1.662–1.662 | n/a |
| GPU exclusive stage sum | 1.046 | 1.046–1.046 | n/a |
| GPU bakeSunShadowMap | 0.096 | 0.096–0.096 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.102 | 0.102–0.102 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.605 | 0.605–0.605 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.067 | 0.067–0.067 | n/a |
| input_to_present mean | 64.297 | 64.297–64.297 | n/a |
| input_to_present p99 | 76.939 | 76.939–76.939 | n/a |
| cursor_to_present mean | 63.060 | 63.060–63.060 | n/a |
| cursor_to_present p99 | 75.390 | 75.390–75.390 | n/a |
| frame_start_to_submit mean | 37.062 | 37.062–37.062 | n/a |
| frame_start_to_submit p99 | 56.463 | 56.463–56.463 | n/a |
| submit_to_present mean | 29.057 | 29.057–29.057 | n/a |
| submit_to_present p99 | 47.419 | 47.419–47.419 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.915 | 47.515 | 56.500 | 64.399 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.70 | 2.70–2.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.3897 |

Render device counters: unsupported by this backend.
