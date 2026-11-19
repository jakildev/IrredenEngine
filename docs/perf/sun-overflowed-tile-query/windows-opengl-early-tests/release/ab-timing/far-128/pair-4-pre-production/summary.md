| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 103.360 | 103.360–103.360 | n/a |
| frame p95 | 112.300 | 112.300–112.300 | n/a |
| frame p99 | 126.300 | 126.300–126.300 | n/a |
| steady frame avg | 102.200 | 102.200–102.200 | n/a |
| steady frame p95 | 111.010 | 111.010–111.010 | n/a |
| steady frame p99 | 115.280 | 115.280–115.280 | n/a |
| frame split INPUT | 0.692 | 0.692–0.692 | n/a |
| frame split UPDATE | 7.115 | 7.115–7.115 | n/a |
| frame split RENDER | 15.640 | 15.640–15.640 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 79.915 | 79.915–79.915 | n/a |
| GPU exclusive stage sum | 64.657 | 64.657–64.657 | n/a |
| GPU bakeSunShadowMap | 0.122 | 0.122–0.122 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.011 | 0.011–0.011 | n/a |
| GPU cardinalSurfaceFb | 0.144 | 0.144–0.144 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.029 | 0.029–0.029 | n/a |
| GPU entityCanvasToFb | 64.115 | 64.115–64.115 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.025 | 0.025–0.025 | n/a |
| GPU voxelCompact | 0.011 | 0.011–0.011 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |
| input_to_present mean | 197.948 | 197.948–197.948 | n/a |
| input_to_present p99 | 214.067 | 214.067–214.067 | n/a |
| cursor_to_present mean | 196.804 | 196.804–196.804 | n/a |
| cursor_to_present p99 | 212.991 | 212.991–212.991 | n/a |
| frame_start_to_submit mean | 21.172 | 21.172–21.172 | n/a |
| frame_start_to_submit p99 | 31.391 | 31.391–31.391 | n/a |
| submit_to_present mean | 183.139 | 183.139–183.139 | n/a |
| submit_to_present p99 | 200.407 | 200.407–200.407 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 102.203 | 111.012 | 115.277 | 116.207 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1402 |

Render device counters: unsupported by this backend.
