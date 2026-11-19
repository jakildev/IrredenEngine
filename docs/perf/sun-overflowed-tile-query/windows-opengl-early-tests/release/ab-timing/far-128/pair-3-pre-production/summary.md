| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.990 | 102.990–102.990 | n/a |
| frame p95 | 112.700 | 112.700–112.700 | n/a |
| frame p99 | 116.710 | 116.710–116.710 | n/a |
| steady frame avg | 101.850 | 101.850–101.850 | n/a |
| steady frame p95 | 112.280 | 112.280–112.280 | n/a |
| steady frame p99 | 116.360 | 116.360–116.360 | n/a |
| frame split INPUT | 0.648 | 0.648–0.648 | n/a |
| frame split UPDATE | 7.117 | 7.117–7.117 | n/a |
| frame split RENDER | 15.635 | 15.635–15.635 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 79.589 | 79.589–79.589 | n/a |
| GPU exclusive stage sum | 64.695 | 64.695–64.695 | n/a |
| GPU bakeSunShadowMap | 0.094 | 0.094–0.094 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.132 | 0.132–0.132 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 | n/a |
| GPU entityCanvasToFb | 64.189 | 64.189–64.189 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.024 | 0.024–0.024 | n/a |
| GPU voxelStage2 | 0.015 | 0.015–0.015 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |
| input_to_present mean | 197.237 | 197.237–197.237 | n/a |
| input_to_present p99 | 219.935 | 219.935–219.935 | n/a |
| cursor_to_present mean | 196.066 | 196.066–196.066 | n/a |
| cursor_to_present p99 | 218.967 | 218.967–218.967 | n/a |
| frame_start_to_submit mean | 21.060 | 21.060–21.060 | n/a |
| frame_start_to_submit p99 | 27.852 | 27.852–27.852 | n/a |
| submit_to_present mean | 182.568 | 182.568–182.568 | n/a |
| submit_to_present p99 | 207.337 | 207.337–207.337 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.851 | 112.281 | 116.360 | 116.713 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.0875 |

Render device counters: unsupported by this backend.
