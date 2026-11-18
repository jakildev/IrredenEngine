| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 131.920 | 131.920–131.920 | n/a |
| frame p95 | 197.880 | 197.880–197.880 | n/a |
| frame p99 | 229.940 | 229.940–229.940 | n/a |
| steady frame avg | 122.910 | 122.910–122.910 | n/a |
| steady frame p95 | 166.790 | 166.790–166.790 | n/a |
| steady frame p99 | 183.880 | 183.880–183.880 | n/a |
| frame split INPUT | 0.637 | 0.637–0.637 | n/a |
| frame split UPDATE | 84.046 | 84.046–84.046 | n/a |
| frame split RENDER | 45.168 | 45.168–45.168 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 2.071 | 2.071–2.071 | n/a |
| GPU exclusive stage sum | 50.967 | 50.967–50.967 | n/a |
| GPU bakeSunShadowMap | 0.130 | 0.130–0.130 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 50.440 | 50.440–50.440 | n/a |
| GPU fbToScreen | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.026 | 0.026–0.026 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.018 | 0.018–0.018 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.029 | 0.029–0.029 | n/a |
| GPU voxelStage2 | 0.012 | 0.012–0.012 | n/a |
| GPU voxelSunFaces | 0.057 | 0.057–0.057 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 122.914 | 166.789 | 183.881 | 191.810 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.10 | 7.10–7.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
