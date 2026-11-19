| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.420 | 36.420–36.420 | n/a |
| frame p95 | 42.580 | 42.580–42.580 | n/a |
| frame p99 | 56.660 | 56.660–56.660 | n/a |
| steady frame avg | 36.000 | 36.000–36.000 | n/a |
| steady frame p95 | 41.370 | 41.370–41.370 | n/a |
| steady frame p99 | 43.380 | 43.380–43.380 | n/a |
| frame split INPUT | 0.084 | 0.084–0.084 | n/a |
| frame split UPDATE | 2.651 | 2.651–2.651 | n/a |
| frame split RENDER | 15.064 | 15.064–15.064 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 18.618 | 18.618–18.618 | n/a |
| GPU exclusive stage sum | 3.308 | 3.308–3.308 | n/a |
| GPU bakeSunShadowMap | 0.085 | 0.085–0.085 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.118 | 0.118–0.118 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.023 | 0.023–0.023 | n/a |
| GPU entityCanvasToFb | 2.857 | 2.857–2.857 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.030 | 0.030–0.030 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.055 | 0.055–0.055 | n/a |
| input_to_present mean | 70.300 | 70.300–70.300 | n/a |
| input_to_present p99 | 83.783 | 83.783–83.783 | n/a |
| cursor_to_present mean | 69.114 | 69.114–69.114 | n/a |
| cursor_to_present p99 | 82.200 | 82.200–82.200 | n/a |
| frame_start_to_submit mean | 15.267 | 15.267–15.267 | n/a |
| frame_start_to_submit p99 | 22.614 | 22.614–22.614 | n/a |
| submit_to_present mean | 56.665 | 56.665–56.665 | n/a |
| submit_to_present p99 | 69.400 | 69.400–69.400 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 36.003 | 41.369 | 43.380 | 45.001 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.20 | 2.20–2.20 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.3862 |

Render device counters: unsupported by this backend.
