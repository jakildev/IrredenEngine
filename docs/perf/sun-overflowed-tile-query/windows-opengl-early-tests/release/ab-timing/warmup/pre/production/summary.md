| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.040 | 102.040–102.040 | n/a |
| frame p95 | 111.420 | 111.420–111.420 | n/a |
| frame p99 | 128.810 | 128.810–128.810 | n/a |
| steady frame avg | 101.010 | 101.010–101.010 | n/a |
| steady frame p95 | 110.680 | 110.680–110.680 | n/a |
| steady frame p99 | 116.880 | 116.880–116.880 | n/a |
| frame split INPUT | 0.640 | 0.640–0.640 | n/a |
| frame split UPDATE | 7.040 | 7.040–7.040 | n/a |
| frame split RENDER | 15.343 | 15.343–15.343 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 79.019 | 79.019–79.019 | n/a |
| GPU exclusive stage sum | 64.120 | 64.120–64.120 | n/a |
| GPU bakeSunShadowMap | 0.109 | 0.109–0.109 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.136 | 0.136–0.136 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.028 | 0.028–0.028 | n/a |
| GPU entityCanvasToFb | 63.603 | 63.603–63.603 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.061 | 0.061–0.061 | n/a |
| input_to_present mean | 195.763 | 195.763–195.763 | n/a |
| input_to_present p99 | 220.122 | 220.122–220.122 | n/a |
| cursor_to_present mean | 194.634 | 194.634–194.634 | n/a |
| cursor_to_present p99 | 218.915 | 218.915–218.915 | n/a |
| frame_start_to_submit mean | 20.667 | 20.667–20.667 | n/a |
| frame_start_to_submit p99 | 29.786 | 29.786–29.786 | n/a |
| submit_to_present mean | 181.248 | 181.248–181.248 | n/a |
| submit_to_present p99 | 206.072 | 206.072–206.072 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.013 | 110.676 | 116.885 | 128.809 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1229 |

Render device counters: unsupported by this backend.
