| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 139.240 | 139.240–139.240 | n/a |
| frame p95 | 216.170 | 216.170–216.170 | n/a |
| frame p99 | 260.000 | 260.000–260.000 | n/a |
| steady frame avg | 133.420 | 133.420–133.420 | n/a |
| steady frame p95 | 166.240 | 166.240–166.240 | n/a |
| steady frame p99 | 187.180 | 187.180–187.180 | n/a |
| frame split INPUT | 0.249 | 0.249–0.249 | n/a |
| frame split UPDATE | 92.946 | 92.946–92.946 | n/a |
| frame split RENDER | 42.103 | 42.103–42.103 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 3.942 | 3.942–3.942 | n/a |
| GPU exclusive stage sum | 1.935 | 1.935–1.935 | n/a |
| GPU bakeSunShadowMap | 0.163 | 0.163–0.163 | n/a |
| GPU buildLightOcclusionGrid | 0.001 | 0.001–0.001 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU cardinalSurfaceFb | 0.097 | 0.097–0.097 | n/a |
| GPU computeLightVolume | 0.037 | 0.037–0.037 | n/a |
| GPU computeSunShadow | 0.032 | 0.032–0.032 | n/a |
| GPU computeVoxelAO | 0.012 | 0.012–0.012 | n/a |
| GPU entityCanvasToFb | 1.228 | 1.228–1.228 | n/a |
| GPU fbToScreen | 0.025 | 0.025–0.025 | n/a |
| GPU lightingToTrixel | 0.045 | 0.045–0.045 | n/a |
| GPU resolvePerAxisScreenDepth | 0.001 | 0.001–0.001 | n/a |
| GPU trixelToFb | 0.023 | 0.023–0.023 | n/a |
| GPU voxelCardinalElect | 0.026 | 0.026–0.026 | n/a |
| GPU voxelCompact | 0.014 | 0.014–0.014 | n/a |
| GPU voxelStage1 | 0.100 | 0.100–0.100 | n/a |
| GPU voxelStage2 | 0.033 | 0.033–0.033 | n/a |
| GPU voxelSunFaces | 0.088 | 0.088–0.088 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 133.419 | 166.238 | 187.184 | 198.660 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 7.40 | 7.40–7.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | not measured | n/a | not measured | n/a |

Render device counters: unsupported by this backend.
