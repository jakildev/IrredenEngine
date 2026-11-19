| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 42.950 | 42.950–42.950 | n/a |
| frame p95 | 46.630 | 46.630–46.630 | n/a |
| frame p99 | 72.910 | 72.910–72.910 | n/a |
| steady frame avg | 42.080 | 42.080–42.080 | n/a |
| steady frame p95 | 44.830 | 44.830–44.830 | n/a |
| steady frame p99 | 45.920 | 45.920–45.920 | n/a |
| frame split INPUT | 0.099 | 0.099–0.099 | n/a |
| frame split UPDATE | 3.265 | 3.265–3.265 | n/a |
| frame split RENDER | 16.743 | 16.743–16.743 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 22.838 | 22.838–22.838 | n/a |
| GPU exclusive stage sum | 3.825 | 3.825–3.825 | n/a |
| GPU bakeSunShadowMap | 0.093 | 0.093–0.093 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.123 | 0.123–0.123 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.025 | 0.025–0.025 | n/a |
| GPU entityCanvasToFb | 3.339 | 3.339–3.339 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.065 | 0.065–0.065 | n/a |
| input_to_present mean | 81.835 | 81.835–81.835 | n/a |
| input_to_present p99 | 88.451 | 88.451–88.451 | n/a |
| cursor_to_present mean | 80.590 | 80.590–80.590 | n/a |
| cursor_to_present p99 | 87.410 | 87.410–87.410 | n/a |
| frame_start_to_submit mean | 17.173 | 17.173–17.173 | n/a |
| frame_start_to_submit p99 | 25.927 | 25.927–25.927 | n/a |
| submit_to_present mean | 66.883 | 66.883–66.883 | n/a |
| submit_to_present p99 | 74.130 | 74.130–74.130 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 42.076 | 44.830 | 45.921 | 46.630 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.60 | 2.60–2.60 | 7 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1177 |

Render device counters: unsupported by this backend.
