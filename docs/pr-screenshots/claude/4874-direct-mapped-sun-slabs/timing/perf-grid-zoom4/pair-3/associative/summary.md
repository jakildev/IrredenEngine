| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 39.650 | 39.650–39.650 | n/a |
| frame p95 | 38.250 | 38.250–38.250 | n/a |
| frame p99 | 688.300 | 688.300–688.300 | n/a |
| steady frame avg | 29.600 | 29.600–29.600 | n/a |
| steady frame p95 | 35.280 | 35.280–35.280 | n/a |
| steady frame p99 | 38.250 | 38.250–38.250 | n/a |
| GPU frame commandBufferSpans | 7.624 | 7.624–7.624 | n/a |
| GPU frame envelope | 7.624 | 7.624–7.624 | n/a |
| GPU bakeSunShadowMap | 0.127 | 0.127–0.127 | n/a |
| GPU canvasClear | 0.034 | 0.034–0.034 | n/a |
| GPU computeLightVolume | 6.072 | 6.072–6.072 | n/a |
| GPU computeSunShadow | 0.058 | 0.058–0.058 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.013 | 0.013–0.013 | n/a |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 | n/a |
| GPU textToTrixel | 1.088 | 1.088–1.088 | n/a |
| GPU trixelToFb | 0.443 | 0.443–0.443 | n/a |
| GPU voxelCardinalElect | 1.791 | 1.791–1.791 | n/a |
| GPU voxelCompact | 0.318 | 0.318–0.318 | n/a |
| GPU voxelStage1 | 1.875 | 1.875–1.875 | n/a |
| GPU voxelStage2 | 1.774 | 1.774–1.774 | n/a |
| GPU voxelSunFaces | 0.973 | 0.973–0.973 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 29.601 | 35.277 | 38.250 | 38.250 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
