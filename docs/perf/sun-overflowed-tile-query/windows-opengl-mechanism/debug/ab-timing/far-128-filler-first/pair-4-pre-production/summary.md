| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 141.920 | 141.920–141.920 | n/a |
| frame p95 | 217.710 | 217.710–217.710 | n/a |
| frame p99 | 249.060 | 249.060–249.060 | n/a |
| steady frame avg | 133.620 | 133.620–133.620 | n/a |
| steady frame p95 | 168.440 | 168.440–168.440 | n/a |
| steady frame p99 | 185.110 | 185.110–185.110 | n/a |
| frame split INPUT | 0.309 | 0.309–0.309 | n/a |
| frame split UPDATE | 92.154 | 92.154–92.154 | n/a |
| frame split RENDER | 45.027 | 45.027–45.027 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.431 | 4.431–4.431 | n/a |
| GPU exclusive stage sum | 48.824 | 48.824–48.824 | n/a |
| GPU bakeSunShadowMap | 0.128 | 0.128–0.128 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.122 | 0.122–0.122 | n/a |
| GPU computeLightVolume | 0.028 | 0.028–0.028 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 48.253 | 48.253–48.253 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.019 | 0.019–0.019 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.053 | 0.053–0.053 | n/a |
| GPU voxelStage2 | 0.018 | 0.018–0.018 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.625 | 168.439 | 185.111 | 196.856 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.50 | 7.50–7.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
