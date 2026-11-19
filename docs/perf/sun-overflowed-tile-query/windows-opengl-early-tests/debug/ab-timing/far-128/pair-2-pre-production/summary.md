| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 103.260 | 103.260–103.260 | n/a |
| frame p95 | 112.370 | 112.370–112.370 | n/a |
| frame p99 | 188.630 | 188.630–188.630 | n/a |
| steady frame avg | 101.670 | 101.670–101.670 | n/a |
| steady frame p95 | 110.300 | 110.300–110.300 | n/a |
| steady frame p99 | 116.070 | 116.070–116.070 | n/a |
| frame split INPUT | 0.567 | 0.567–0.567 | n/a |
| frame split UPDATE | 7.459 | 7.459–7.459 | n/a |
| frame split RENDER | 38.284 | 38.284–38.284 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 56.945 | 56.945–56.945 | n/a |
| GPU exclusive stage sum | 64.218 | 64.218–64.218 | n/a |
| GPU bakeSunShadowMap | 0.094 | 0.094–0.094 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.039 | 0.039–0.039 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.018 | 0.018–0.018 | n/a |
| GPU entityCanvasToFb | 63.728 | 63.728–63.728 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.065 | 0.065–0.065 | n/a |
| input_to_present mean | 196.624 | 196.624–196.624 | n/a |
| input_to_present p99 | 214.234 | 214.234–214.234 | n/a |
| cursor_to_present mean | 195.434 | 195.434–195.434 | n/a |
| cursor_to_present p99 | 213.154 | 213.154–213.154 | n/a |
| frame_start_to_submit mean | 40.352 | 40.352–40.352 | n/a |
| frame_start_to_submit p99 | 55.763 | 55.763–55.763 | n/a |
| submit_to_present mean | 162.867 | 162.867–162.867 | n/a |
| submit_to_present p99 | 183.641 | 183.641–183.641 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.669 | 110.298 | 116.074 | 117.738 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1121 |

Render device counters: unsupported by this backend.
