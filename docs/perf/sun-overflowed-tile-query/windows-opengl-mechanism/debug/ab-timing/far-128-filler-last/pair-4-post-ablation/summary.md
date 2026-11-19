| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 120.900 | 120.900–120.900 | n/a |
| frame p95 | 205.090 | 205.090–205.090 | n/a |
| frame p99 | 236.310 | 236.310–236.310 | n/a |
| steady frame avg | 110.040 | 110.040–110.040 | n/a |
| steady frame p95 | 143.190 | 143.190–143.190 | n/a |
| steady frame p99 | 167.280 | 167.280–167.280 | n/a |
| frame split INPUT | 0.196 | 0.196–0.196 | n/a |
| frame split UPDATE | 76.609 | 76.609–76.609 | n/a |
| frame split RENDER | 42.593 | 42.593–42.593 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.506 | 1.506–1.506 | n/a |
| GPU exclusive stage sum | 1.874 | 1.874–1.874 | n/a |
| GPU bakeSunShadowMap | 0.151 | 0.151–0.151 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.005 | 0.005–0.005 | n/a |
| GPU cardinalSurfaceFb | 0.089 | 0.089–0.089 | n/a |
| GPU computeLightVolume | 0.034 | 0.034–0.034 | n/a |
| GPU computeSunShadow | 0.030 | 0.030–0.030 | n/a |
| GPU computeVoxelAO | 0.010 | 0.010–0.010 | n/a |
| GPU entityCanvasToFb | 1.313 | 1.313–1.313 | n/a |
| GPU fbToScreen | 0.021 | 0.021–0.021 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.024 | 0.024–0.024 | n/a |
| GPU voxelCompact | 0.013 | 0.013–0.013 | n/a |
| GPU voxelStage1 | 0.036 | 0.036–0.036 | n/a |
| GPU voxelStage2 | 0.014 | 0.014–0.014 | n/a |
| GPU voxelSunFaces | 0.077 | 0.077–0.077 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 110.039 | 143.186 | 167.279 | 170.746 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.70 | 6.70–6.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
