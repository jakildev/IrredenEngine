| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 104.140 | 104.140–104.140 | n/a |
| frame p95 | 115.260 | 115.260–115.260 | n/a |
| frame p99 | 223.420 | 223.420–223.420 | n/a |
| steady frame avg | 101.350 | 101.350–101.350 | n/a |
| steady frame p95 | 112.080 | 112.080–112.080 | n/a |
| steady frame p99 | 116.750 | 116.750–116.750 | n/a |
| frame split INPUT | 0.563 | 0.563–0.563 | n/a |
| frame split UPDATE | 7.374 | 7.374–7.374 | n/a |
| frame split RENDER | 40.388 | 40.388–40.388 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 55.811 | 55.811–55.811 | n/a |
| GPU exclusive stage sum | 63.849 | 63.849–63.849 | n/a |
| GPU bakeSunShadowMap | 0.089 | 0.089–0.089 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.136 | 0.136–0.136 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.013 | 0.013–0.013 | n/a |
| GPU entityCanvasToFb | 63.358 | 63.358–63.358 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.030 | 0.030–0.030 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.026 | 0.026–0.026 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.061 | 0.061–0.061 | n/a |
| input_to_present mean | 196.107 | 196.107–196.107 | n/a |
| input_to_present p99 | 218.313 | 218.313–218.313 | n/a |
| cursor_to_present mean | 194.937 | 194.937–194.937 | n/a |
| cursor_to_present p99 | 217.228 | 217.228–217.228 | n/a |
| frame_start_to_submit mean | 40.821 | 40.821–40.821 | n/a |
| frame_start_to_submit p99 | 54.213 | 54.213–54.213 | n/a |
| submit_to_present mean | 161.770 | 161.770–161.770 | n/a |
| submit_to_present p99 | 188.197 | 188.197–188.197 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 101.349 | 112.076 | 116.750 | 119.090 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.4151 |

Render device counters: unsupported by this backend.
