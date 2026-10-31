| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.590 | 34.590–34.590 | n/a |
| frame p95 | 27.930 | 27.930–27.930 | n/a |
| frame p99 | 646.680 | 646.680–646.680 | n/a |
| steady frame avg | 23.610 | 23.610–23.610 | n/a |
| steady frame p95 | 26.380 | 26.380–26.380 | n/a |
| steady frame p99 | 27.770 | 27.770–27.770 | n/a |
| GPU frame commandBufferSpans | 7.448 | 7.448–7.448 | n/a |
| GPU frame envelope | 7.448 | 7.448–7.448 | n/a |
| GPU bakeSunShadowMap | 0.052 | 0.052–0.052 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 6.010 | 6.010–6.010 | n/a |
| GPU computeSunShadow | 0.145 | 0.145–0.145 | n/a |
| GPU computeVoxelAO | 0.051 | 0.051–0.051 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.000 | 1.000–1.000 | n/a |
| GPU trixelToFb | 0.881 | 0.881–0.881 | n/a |
| GPU voxelCardinalElect | 1.783 | 1.783–1.783 | n/a |
| GPU voxelCompact | 0.793 | 0.793–0.793 | n/a |
| GPU voxelStage1 | 1.894 | 1.894–1.894 | n/a |
| GPU voxelStage2 | 1.780 | 1.780–1.780 | n/a |
| GPU voxelSunFaces | 1.018 | 1.018–1.018 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 23.613 | 26.379 | 27.770 | 27.770 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
