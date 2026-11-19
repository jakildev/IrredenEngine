| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 48.090 | 48.090–48.090 | n/a |
| frame p95 | 115.510 | 115.510–115.510 | n/a |
| frame p99 | 144.330 | 144.330–144.330 | n/a |
| steady frame avg | 40.310 | 40.310–40.310 | n/a |
| steady frame p95 | 50.310 | 50.310–50.310 | n/a |
| steady frame p99 | 65.840 | 65.840–65.840 | n/a |
| frame split INPUT | 0.087 | 0.087–0.087 | n/a |
| frame split UPDATE | 3.501 | 3.501–3.501 | n/a |
| frame split RENDER | 42.440 | 42.440–42.440 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.059 | 2.059–2.059 | n/a |
| GPU exclusive stage sum | 1.163 | 1.163–1.163 | n/a |
| GPU bakeSunShadowMap | 0.103 | 0.103–0.103 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.090 | 0.090–0.090 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 0.722 | 0.722–0.722 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.025 | 0.025–0.025 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.070 | 0.070–0.070 | n/a |
| input_to_present mean | 62.272 | 62.272–62.272 | n/a |
| input_to_present p99 | 78.788 | 78.788–78.788 | n/a |
| cursor_to_present mean | 61.061 | 61.061–61.061 | n/a |
| cursor_to_present p99 | 77.606 | 77.606–77.606 | n/a |
| frame_start_to_submit mean | 37.920 | 37.920–37.920 | n/a |
| frame_start_to_submit p99 | 65.814 | 65.814–65.814 | n/a |
| submit_to_present mean | 26.271 | 26.271–26.271 | n/a |
| submit_to_present p99 | 48.731 | 48.731–48.731 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 40.308 | 50.310 | 65.838 | 69.660 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.90 | 2.90–2.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.091 |

Render device counters: unsupported by this backend.
