| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.290 | 46.290–46.290 | n/a |
| frame p95 | 112.670 | 112.670–112.670 | n/a |
| frame p99 | 142.670 | 142.670–142.670 | n/a |
| steady frame avg | 38.830 | 38.830–38.830 | n/a |
| steady frame p95 | 47.880 | 47.880–47.880 | n/a |
| steady frame p99 | 54.350 | 54.350–54.350 | n/a |
| frame split INPUT | 0.104 | 0.104–0.104 | n/a |
| frame split UPDATE | 3.337 | 3.337–3.337 | n/a |
| frame split RENDER | 40.850 | 40.850–40.850 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.998 | 1.998–1.998 | n/a |
| GPU exclusive stage sum | 1.086 | 1.086–1.086 | n/a |
| GPU bakeSunShadowMap | 0.095 | 0.095–0.095 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.006 | 0.006–0.006 | n/a |
| GPU cardinalSurfaceFb | 0.091 | 0.091–0.091 | n/a |
| GPU computeLightVolume | 0.031 | 0.031–0.031 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.650 | 0.650–0.650 | n/a |
| GPU fbToScreen | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.071 | 0.071–0.071 | n/a |
| input_to_present mean | 65.524 | 65.524–65.524 | n/a |
| input_to_present p99 | 78.537 | 78.537–78.537 | n/a |
| cursor_to_present mean | 64.308 | 64.308–64.308 | n/a |
| cursor_to_present p99 | 77.416 | 77.416–77.416 | n/a |
| frame_start_to_submit mean | 36.751 | 36.751–36.751 | n/a |
| frame_start_to_submit p99 | 54.237 | 54.237–54.237 | n/a |
| submit_to_present mean | 30.574 | 30.574–30.574 | n/a |
| submit_to_present p99 | 48.417 | 48.417–48.417 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.832 | 47.881 | 54.348 | 62.127 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.70 | 2.70–2.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.3596 |

Render device counters: unsupported by this backend.
