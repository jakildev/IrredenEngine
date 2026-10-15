| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 19.020 | 19.020–19.020 |
| frame p95 | 20.720 | 20.720–20.720 |
| frame p99 | 21.500 | 21.500–21.500 |
| steady frame avg | 18.900 | 18.900–18.900 |
| steady frame p95 | 20.660 | 20.660–20.660 |
| steady frame p99 | 20.780 | 20.780–20.780 |
| GPU frame commandBufferSpans | 4.429 | 4.429–4.429 |
| GPU frame envelope | 4.429 | 4.429–4.429 |
| GPU canvasClear | 0.049 | 0.049–0.049 |
| GPU computeLightVolume | 2.422 | 2.422–2.422 |
| GPU computeSunShadow | 0.059 | 0.059–0.059 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.122 | 0.122–0.122 |
| GPU fogToTrixel | 0.030 | 0.030–0.030 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 0.965 | 0.965–0.965 |
| GPU trixelToFb | 0.265 | 0.265–0.265 |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 |
| GPU voxelCompact | 0.397 | 0.397–0.397 |
| GPU voxelStage1 | 0.541 | 0.541–0.541 |
| GPU voxelStage2 | 0.464 | 0.464–0.464 |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.897 | 20.659 | 20.784 | 21.248 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
