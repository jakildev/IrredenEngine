| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.830 | 46.830–46.830 | n/a |
| frame p95 | 121.610 | 121.610–121.610 | n/a |
| frame p99 | 143.540 | 143.540–143.540 | n/a |
| steady frame avg | 38.630 | 38.630–38.630 | n/a |
| steady frame p95 | 44.580 | 44.580–44.580 | n/a |
| steady frame p99 | 50.450 | 50.450–50.450 | n/a |
| frame split INPUT | 0.095 | 0.095–0.095 | n/a |
| frame split UPDATE | 3.321 | 3.321–3.321 | n/a |
| frame split RENDER | 40.476 | 40.476–40.476 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.942 | 2.942–2.942 | n/a |
| GPU exclusive stage sum | 1.083 | 1.083–1.083 | n/a |
| GPU bakeSunShadowMap | 0.090 | 0.090–0.090 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.087 | 0.087–0.087 | n/a |
| GPU computeLightVolume | 0.040 | 0.040–0.040 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 0.644 | 0.644–0.644 | n/a |
| GPU fbToScreen | 0.023 | 0.023–0.023 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.067 | 0.067–0.067 | n/a |
| input_to_present mean | 70.166 | 70.166–70.166 | n/a |
| input_to_present p99 | 79.315 | 79.315–79.315 | n/a |
| cursor_to_present mean | 68.987 | 68.987–68.987 | n/a |
| cursor_to_present p99 | 78.075 | 78.075–78.075 | n/a |
| frame_start_to_submit mean | 35.172 | 35.172–35.172 | n/a |
| frame_start_to_submit p99 | 50.222 | 50.222–50.222 | n/a |
| submit_to_present mean | 36.746 | 36.746–36.746 | n/a |
| submit_to_present p99 | 49.992 | 49.992–49.992 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.627 | 44.577 | 50.451 | 70.266 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0798 |

Render device counters: unsupported by this backend.
