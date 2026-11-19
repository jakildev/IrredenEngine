| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 46.840 | 46.840–46.840 | n/a |
| frame p95 | 115.140 | 115.140–115.140 | n/a |
| frame p99 | 141.520 | 141.520–141.520 | n/a |
| steady frame avg | 38.990 | 38.990–38.990 | n/a |
| steady frame p95 | 47.000 | 47.000–47.000 | n/a |
| steady frame p99 | 56.100 | 56.100–56.100 | n/a |
| frame split INPUT | 0.321 | 0.321–0.321 | n/a |
| frame split UPDATE | 3.401 | 3.401–3.401 | n/a |
| frame split RENDER | 40.776 | 40.776–40.776 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.342 | 2.342–2.342 | n/a |
| GPU exclusive stage sum | 1.066 | 1.066–1.066 | n/a |
| GPU bakeSunShadowMap | 0.096 | 0.096–0.096 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.082 | 0.082–0.082 | n/a |
| GPU computeLightVolume | 0.033 | 0.033–0.033 | n/a |
| GPU computeSunShadow | 0.001 | 0.001–0.001 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 0.641 | 0.641–0.641 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.022 | 0.022–0.022 | n/a |
| GPU voxelStage2 | 0.010 | 0.010–0.010 | n/a |
| GPU voxelSunFaces | 0.067 | 0.067–0.067 | n/a |
| input_to_present mean | 65.925 | 65.925–65.925 | n/a |
| input_to_present p99 | 78.182 | 78.182–78.182 | n/a |
| cursor_to_present mean | 64.692 | 64.692–64.692 | n/a |
| cursor_to_present p99 | 76.988 | 76.988–76.988 | n/a |
| frame_start_to_submit mean | 36.187 | 36.187–36.187 | n/a |
| frame_start_to_submit p99 | 56.069 | 56.069–56.069 | n/a |
| submit_to_present mean | 31.586 | 31.586–31.586 | n/a |
| submit_to_present p99 | 48.738 | 48.738–48.738 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 38.989 | 46.998 | 56.104 | 56.835 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 2.80 | 2.80–2.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 269 / 270 | 1 | 0.3891 |

Render device counters: unsupported by this backend.
