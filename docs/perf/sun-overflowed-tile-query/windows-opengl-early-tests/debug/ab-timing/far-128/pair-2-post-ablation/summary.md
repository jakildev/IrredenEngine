| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 47.220 | 47.220–47.220 | n/a |
| frame p95 | 121.230 | 121.230–121.230 | n/a |
| frame p99 | 151.190 | 151.190–151.190 | n/a |
| steady frame avg | 39.150 | 39.150–39.150 | n/a |
| steady frame p95 | 50.760 | 50.760–50.760 | n/a |
| steady frame p99 | 58.920 | 58.920–58.920 | n/a |
| frame split INPUT | 0.084 | 0.084–0.084 | n/a |
| frame split UPDATE | 3.488 | 3.488–3.488 | n/a |
| frame split RENDER | 42.160 | 42.160–42.160 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.484 | 1.484–1.484 | n/a |
| GPU exclusive stage sum | 1.169 | 1.169–1.169 | n/a |
| GPU bakeSunShadowMap | 0.100 | 0.100–0.100 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.087 | 0.087–0.087 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 0.720 | 0.720–0.720 | n/a |
| GPU fbToScreen | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.027 | 0.027–0.027 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.037 | 0.037–0.037 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelStage2 | 0.011 | 0.011–0.011 | n/a |
| GPU voxelSunFaces | 0.069 | 0.069–0.069 | n/a |
| input_to_present mean | 65.929 | 65.929–65.929 | n/a |
| input_to_present p99 | 77.698 | 77.698–77.698 | n/a |
| cursor_to_present mean | 64.678 | 64.678–64.678 | n/a |
| cursor_to_present p99 | 76.223 | 76.223–76.223 | n/a |
| frame_start_to_submit mean | 37.559 | 37.559–37.559 | n/a |
| frame_start_to_submit p99 | 58.883 | 58.883–58.883 | n/a |
| submit_to_present mean | 30.276 | 30.276–30.276 | n/a |
| submit_to_present p99 | 47.536 | 47.536–47.536 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 39.149 | 50.762 | 58.918 | 65.463 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.2331 |

Render device counters: unsupported by this backend.
