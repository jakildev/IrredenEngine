| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 97.770 | 97.770–97.770 | n/a |
| frame p95 | 186.020 | 186.020–186.020 | n/a |
| frame p99 | 223.180 | 223.180–223.180 | n/a |
| steady frame avg | 87.860 | 87.860–87.860 | n/a |
| steady frame p95 | 125.300 | 125.300–125.300 | n/a |
| steady frame p99 | 140.900 | 140.900–140.900 | n/a |
| frame split INPUT | 0.142 | 0.142–0.142 | n/a |
| frame split UPDATE | 58.608 | 58.608–58.608 | n/a |
| frame split RENDER | 38.462 | 38.462–38.462 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.555 | 0.555–0.555 | n/a |
| GPU exclusive stage sum | 1.570 | 1.570–1.570 | n/a |
| GPU bakeSunShadowMap | 0.131 | 0.131–0.131 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.009 | 0.009–0.009 | n/a |
| GPU cardinalSurfaceFb | 0.078 | 0.078–0.078 | n/a |
| GPU computeLightVolume | 0.032 | 0.032–0.032 | n/a |
| GPU computeSunShadow | 0.027 | 0.027–0.027 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.007 | 1.007–1.007 | n/a |
| GPU fbToScreen | 0.019 | 0.019–0.019 | n/a |
| GPU lightingToTrixel | 0.037 | 0.037–0.037 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.020 | 0.020–0.020 | n/a |
| GPU voxelCardinalElect | 0.022 | 0.022–0.022 | n/a |
| GPU voxelCompact | 0.012 | 0.012–0.012 | n/a |
| GPU voxelStage1 | 0.072 | 0.072–0.072 | n/a |
| GPU voxelStage2 | 0.023 | 0.023–0.023 | n/a |
| GPU voxelSunFaces | 0.067 | 0.067–0.067 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 87.858 | 125.299 | 140.905 | 147.895 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 5.50 | 5.50–5.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
