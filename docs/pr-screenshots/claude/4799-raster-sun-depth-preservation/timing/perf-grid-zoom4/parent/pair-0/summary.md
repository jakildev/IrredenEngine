| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 34.870 | 34.870–34.870 | n/a |
| frame p95 | 32.360 | 32.360–32.360 | n/a |
| frame p99 | 688.280 | 688.280–688.280 | n/a |
| steady frame avg | 24.070 | 24.070–24.070 | n/a |
| steady frame p95 | 30.800 | 30.800–30.800 | n/a |
| steady frame p99 | 32.360 | 32.360–32.360 | n/a |
| GPU frame commandBufferSpans | 7.605 | 7.605–7.605 | n/a |
| GPU frame envelope | 7.605 | 7.605–7.605 | n/a |
| GPU bakeSunShadowMap | 0.035 | 0.035–0.035 | n/a |
| GPU canvasClear | 0.019 | 0.019–0.019 | n/a |
| GPU computeLightVolume | 5.923 | 5.923–5.923 | n/a |
| GPU computeSunShadow | 0.051 | 0.051–0.051 | n/a |
| GPU computeVoxelAO | 0.051 | 0.051–0.051 | n/a |
| GPU fbToScreen | 0.036 | 0.036–0.036 | n/a |
| GPU fogToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 0.639 | 0.639–0.639 | n/a |
| GPU trixelToFb | 1.102 | 1.102–1.102 | n/a |
| GPU voxelCardinalElect | 1.827 | 1.827–1.827 | n/a |
| GPU voxelCompact | 0.274 | 0.274–0.274 | n/a |
| GPU voxelStage1 | 1.978 | 1.978–1.978 | n/a |
| GPU voxelStage2 | 1.781 | 1.781–1.781 | n/a |
| GPU voxelSunFaces | 0.979 | 0.979–0.979 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 24.071 | 30.804 | 32.365 | 32.365 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
