| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.400 | 46.400–46.400 | n/a |
| frame p95 | 119.510 | 119.510–119.510 | n/a |
| frame p99 | 156.080 | 156.080–156.080 | n/a |
| steady frame avg | 37.970 | 37.970–37.970 | n/a |
| steady frame p95 | 43.580 | 43.580–43.580 | n/a |
| steady frame p99 | 48.290 | 48.290–48.290 | n/a |
| frame split INPUT | 0.312 | 0.312–0.312 | n/a |
| frame split UPDATE | 3.342 | 3.342–3.342 | n/a |
| frame split RENDER | 40.468 | 40.468–40.468 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.283 | 2.283–2.283 | n/a |
| GPU exclusive stage sum | 1.196 | 1.196–1.196 | n/a |
| GPU bakeSunShadowMap | 0.105 | 0.105–0.105 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.088 | 0.088–0.088 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.009 | 0.009–0.009 | n/a |
| GPU entityCanvasToFb | 0.761 | 0.761–0.761 | n/a |
| GPU fbToScreen | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.070 | 0.070–0.070 | n/a |
| input_to_present mean | 66.512 | 66.512–66.512 | n/a |
| input_to_present p99 | 77.123 | 77.123–77.123 | n/a |
| cursor_to_present mean | 65.297 | 65.297–65.297 | n/a |
| cursor_to_present p99 | 76.037 | 76.037–76.037 | n/a |
| frame_start_to_submit mean | 35.262 | 35.262–35.262 | n/a |
| frame_start_to_submit p99 | 48.250 | 48.250–48.250 | n/a |
| submit_to_present mean | 32.998 | 32.998–32.998 | n/a |
| submit_to_present p99 | 48.094 | 48.094–48.094 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 37.970 | 43.580 | 48.286 | 52.570 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.70 | 2.70–2.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1743 |

Render device counters: unsupported by this backend.
