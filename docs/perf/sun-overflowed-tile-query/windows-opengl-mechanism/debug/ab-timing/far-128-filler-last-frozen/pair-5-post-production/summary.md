| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 143.200 | 143.200–143.200 | n/a |
| frame p95 | 207.700 | 207.700–207.700 | n/a |
| frame p99 | 261.710 | 261.710–261.710 | n/a |
| steady frame avg | 134.380 | 134.380–134.380 | n/a |
| steady frame p95 | 169.800 | 169.800–169.800 | n/a |
| steady frame p99 | 186.360 | 186.360–186.360 | n/a |
| frame split INPUT | 0.470 | 0.470–0.470 | n/a |
| frame split UPDATE | 91.924 | 91.924–91.924 | n/a |
| frame split RENDER | 46.569 | 46.569–46.569 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.240 | 4.240–4.240 | n/a |
| GPU exclusive stage sum | 40.780 | 40.780–40.780 | n/a |
| GPU bakeSunShadowMap | 0.122 | 0.122–0.122 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.004 | 0.004–0.004 | n/a |
| GPU cardinalSurfaceFb | 0.129 | 0.129–0.129 | n/a |
| GPU computeLightVolume | 0.029 | 0.029–0.029 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 40.249 | 40.249–40.249 | n/a |
| GPU fbToScreen | 0.022 | 0.022–0.022 | n/a |
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
| first 90 of each run excluded | 270 | 134.384 | 169.798 | 186.359 | 193.157 |

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
