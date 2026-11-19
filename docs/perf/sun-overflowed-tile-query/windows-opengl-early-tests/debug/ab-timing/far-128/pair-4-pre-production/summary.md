| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 102.830 | 102.830–102.830 | n/a |
| frame p95 | 112.890 | 112.890–112.890 | n/a |
| frame p99 | 184.170 | 184.170–184.170 | n/a |
| steady frame avg | 100.460 | 100.460–100.460 | n/a |
| steady frame p95 | 109.930 | 109.930–109.930 | n/a |
| steady frame p99 | 114.870 | 114.870–114.870 | n/a |
| frame split INPUT | 0.569 | 0.569–0.569 | n/a |
| frame split UPDATE | 7.352 | 7.352–7.352 | n/a |
| frame split RENDER | 39.339 | 39.339–39.339 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 55.570 | 55.570–55.570 | n/a |
| GPU exclusive stage sum | 63.665 | 63.665–63.665 | n/a |
| GPU bakeSunShadowMap | 0.134 | 0.134–0.134 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.008 | 0.008–0.008 | n/a |
| GPU cardinalSurfaceFb | 0.124 | 0.124–0.124 | n/a |
| GPU computeLightVolume | 0.027 | 0.027–0.027 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.019 | 0.019–0.019 | n/a |
| GPU entityCanvasToFb | 63.138 | 63.138–63.138 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.029 | 0.029–0.029 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCardinalElect | 0.029 | 0.029–0.029 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.025 | 0.025–0.025 | n/a |
| GPU voxelStage2 | 0.013 | 0.013–0.013 | n/a |
| GPU voxelSunFaces | 0.062 | 0.062–0.062 | n/a |
| input_to_present mean | 194.358 | 194.358–194.358 | n/a |
| input_to_present p99 | 213.623 | 213.623–213.623 | n/a |
| cursor_to_present mean | 193.160 | 193.160–193.160 | n/a |
| cursor_to_present p99 | 212.670 | 212.670–212.670 | n/a |
| frame_start_to_submit mean | 40.627 | 40.627–40.627 | n/a |
| frame_start_to_submit p99 | 57.085 | 57.085–57.085 | n/a |
| submit_to_present mean | 160.211 | 160.211–160.211 | n/a |
| submit_to_present p99 | 185.306 | 185.306–185.306 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 100.459 | 109.934 | 114.871 | 115.794 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.10 | 6.10–6.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.1282 |

Render device counters: unsupported by this backend.
