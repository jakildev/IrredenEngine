| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.400 | 37.400–37.400 | n/a |
| frame p95 | 33.660 | 33.660–33.660 | n/a |
| frame p99 | 699.130 | 699.130–699.130 | n/a |
| steady frame avg | 27.190 | 27.190–27.190 | n/a |
| steady frame p95 | 33.200 | 33.200–33.200 | n/a |
| steady frame p99 | 35.310 | 35.310–35.310 | n/a |
| GPU frame commandBufferSpans | 7.538 | 7.538–7.538 | n/a |
| GPU frame envelope | 7.538 | 7.538–7.538 | n/a |
| GPU bakeSunShadowMap | 0.039 | 0.039–0.039 | n/a |
| GPU canvasClear | 0.012 | 0.012–0.012 | n/a |
| GPU computeLightVolume | 6.120 | 6.120–6.120 | n/a |
| GPU computeSunShadow | 0.054 | 0.054–0.054 | n/a |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.017 | 0.017–0.017 | n/a |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 | n/a |
| GPU textToTrixel | 0.648 | 0.648–0.648 | n/a |
| GPU trixelToFb | 0.969 | 0.969–0.969 | n/a |
| GPU voxelCardinalElect | 1.861 | 1.861–1.861 | n/a |
| GPU voxelCompact | 0.194 | 0.194–0.194 | n/a |
| GPU voxelStage1 | 1.976 | 1.976–1.976 | n/a |
| GPU voxelStage2 | 1.850 | 1.850–1.850 | n/a |
| GPU voxelSunFaces | 0.986 | 0.986–0.986 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 27.191 | 33.196 | 35.312 | 35.312 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
