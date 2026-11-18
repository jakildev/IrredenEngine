| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 143.610 | 143.610–143.610 | n/a |
| frame p95 | 226.550 | 226.550–226.550 | n/a |
| frame p99 | 278.670 | 278.670–278.670 | n/a |
| steady frame avg | 132.370 | 132.370–132.370 | n/a |
| steady frame p95 | 169.310 | 169.310–169.310 | n/a |
| steady frame p99 | 176.650 | 176.650–176.650 | n/a |
| frame split INPUT | 0.259 | 0.259–0.259 | n/a |
| frame split UPDATE | 95.163 | 95.163–95.163 | n/a |
| frame split RENDER | 44.009 | 44.009–44.009 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 4.184 | 4.184–4.184 | n/a |
| GPU exclusive stage sum | 2.240 | 2.240–2.240 | n/a |
| GPU bakeSunShadowMap | 0.164 | 0.164–0.164 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.099 | 0.099–0.099 | n/a |
| GPU computeLightVolume | 0.039 | 0.039–0.039 | n/a |
| GPU computeSunShadow | 0.034 | 0.034–0.034 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.527 | 1.527–1.527 | n/a |
| GPU fbToScreen | 0.024 | 0.024–0.024 | n/a |
| GPU lightingToTrixel | 0.046 | 0.046–0.046 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCardinalElect | 0.027 | 0.027–0.027 | n/a |
| GPU voxelCompact | 0.015 | 0.015–0.015 | n/a |
| GPU voxelStage1 | 0.098 | 0.098–0.098 | n/a |
| GPU voxelStage2 | 0.031 | 0.031–0.031 | n/a |
| GPU voxelSunFaces | 0.089 | 0.089–0.089 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 132.369 | 169.307 | 176.650 | 179.593 |

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
