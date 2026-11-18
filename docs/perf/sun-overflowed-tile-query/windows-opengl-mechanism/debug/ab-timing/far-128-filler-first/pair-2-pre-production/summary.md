| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 105.750 | 105.750–105.750 | n/a |
| frame p95 | 185.300 | 185.300–185.300 | n/a |
| frame p99 | 231.580 | 231.580–231.580 | n/a |
| steady frame avg | 94.350 | 94.350–94.350 | n/a |
| steady frame p95 | 121.450 | 121.450–121.450 | n/a |
| steady frame p99 | 142.790 | 142.790–142.790 | n/a |
| frame split INPUT | 0.484 | 0.484–0.484 | n/a |
| frame split UPDATE | 64.645 | 64.645–64.645 | n/a |
| frame split RENDER | 39.781 | 39.781–39.781 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 0.840 | 0.840–0.840 | n/a |
| GPU exclusive stage sum | 48.588 | 48.588–48.588 | n/a |
| GPU bakeSunShadowMap | 0.129 | 0.129–0.129 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.007 | 0.007–0.007 | n/a |
| GPU cardinalSurfaceFb | 0.121 | 0.121–0.121 | n/a |
| GPU computeLightVolume | 0.030 | 0.030–0.030 | n/a |
| GPU computeSunShadow | 0.044 | 0.044–0.044 | n/a |
| GPU computeVoxelAO | 0.008 | 0.008–0.008 | n/a |
| GPU entityCanvasToFb | 47.997 | 47.997–47.997 | n/a |
| GPU fbToScreen | 0.020 | 0.020–0.020 | n/a |
| GPU lightingToTrixel | 0.035 | 0.035–0.035 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCardinalElect | 0.021 | 0.021–0.021 | n/a |
| GPU voxelCompact | 0.010 | 0.010–0.010 | n/a |
| GPU voxelStage1 | 0.062 | 0.062–0.062 | n/a |
| GPU voxelStage2 | 0.020 | 0.020–0.020 | n/a |
| GPU voxelSunFaces | 0.059 | 0.059–0.059 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 94.345 | 121.450 | 142.786 | 144.129 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 6.00 | 6.00–6.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
