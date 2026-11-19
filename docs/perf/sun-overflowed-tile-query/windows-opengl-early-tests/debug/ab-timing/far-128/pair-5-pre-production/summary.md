| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.420 | 102.420–102.420 | n/a |
| frame p95 | 112.440 | 112.440–112.440 | n/a |
| frame p99 | 188.590 | 188.590–188.590 | n/a |
| steady frame avg | 101.090 | 101.090–101.090 | n/a |
| steady frame p95 | 112.120 | 112.120–112.120 | n/a |
| steady frame p99 | 113.670 | 113.670–113.670 | n/a |
| frame split INPUT | 0.645 | 0.645–0.645 | n/a |
| frame split UPDATE | 7.398 | 7.398–7.398 | n/a |
| frame split RENDER | 39.034 | 39.034–39.034 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 55.344 | 55.344–55.344 | n/a |
| GPU exclusive stage sum | 63.595 | 63.595–63.595 | n/a |
| GPU bakeSunShadowMap | 0.104 | 0.104–0.104 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.010 | 0.010–0.010 | n/a |
| GPU cardinalSurfaceFb | 0.116 | 0.116–0.116 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.021 | 0.021–0.021 | n/a |
| GPU entityCanvasToFb | 63.098 | 63.098–63.098 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.033 | 0.033–0.033 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.017 | 0.017–0.017 | n/a |
| GPU voxelSunFaces | 0.068 | 0.068–0.068 | n/a |
| input_to_present mean | 195.405 | 195.405–195.405 | n/a |
| input_to_present p99 | 218.506 | 218.506–218.506 | n/a |
| cursor_to_present mean | 194.199 | 194.199–194.199 | n/a |
| cursor_to_present p99 | 217.163 | 217.163–217.163 | n/a |
| frame_start_to_submit mean | 41.769 | 41.769–41.769 | n/a |
| frame_start_to_submit p99 | 54.713 | 54.713–54.713 | n/a |
| submit_to_present mean | 160.251 | 160.251–160.251 | n/a |
| submit_to_present p99 | 184.574 | 184.574–184.574 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.086 | 112.119 | 113.669 | 124.952 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.00 | 6.00–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.4332 |

Render device counters: unsupported by this backend.
