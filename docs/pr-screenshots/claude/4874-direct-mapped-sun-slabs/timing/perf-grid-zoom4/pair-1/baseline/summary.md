| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.830 | 37.830–37.830 | n/a |
| frame p95 | 36.340 | 36.340–36.340 | n/a |
| frame p99 | 696.210 | 696.210–696.210 | n/a |
| steady frame avg | 27.570 | 27.570–27.570 | n/a |
| steady frame p95 | 33.020 | 33.020–33.020 | n/a |
| steady frame p99 | 36.340 | 36.340–36.340 | n/a |
| GPU frame commandBufferSpans | 7.095 | 7.095–7.095 | n/a |
| GPU frame envelope | 7.095 | 7.095–7.095 | n/a |
| GPU bakeSunShadowMap | 0.118 | 0.118–0.118 | n/a |
| GPU canvasClear | 0.041 | 0.041–0.041 | n/a |
| GPU computeLightVolume | 5.693 | 5.693–5.693 | n/a |
| GPU computeSunShadow | 0.060 | 0.060–0.060 | n/a |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.018 | 0.018–0.018 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 0.713 | 0.713–0.713 | n/a |
| GPU trixelToFb | 0.820 | 0.820–0.820 | n/a |
| GPU voxelCardinalElect | 1.696 | 1.696–1.696 | n/a |
| GPU voxelCompact | 0.735 | 0.735–0.735 | n/a |
| GPU voxelStage1 | 1.880 | 1.880–1.880 | n/a |
| GPU voxelStage2 | 1.621 | 1.621–1.621 | n/a |
| GPU voxelSunFaces | 0.956 | 0.956–0.956 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 27.567 | 33.021 | 36.341 | 36.341 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
