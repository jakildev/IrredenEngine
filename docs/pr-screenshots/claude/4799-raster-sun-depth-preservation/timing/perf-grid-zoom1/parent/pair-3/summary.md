| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 33.450 | 33.450–33.450 | n/a |
| frame p95 | 27.900 | 27.900–27.900 | n/a |
| frame p99 | 678.940 | 678.940–678.940 | n/a |
| steady frame avg | 22.780 | 22.780–22.780 | n/a |
| steady frame p95 | 26.520 | 26.520–26.520 | n/a |
| steady frame p99 | 27.900 | 27.900–27.900 | n/a |
| GPU frame commandBufferSpans | 4.899 | 4.899–4.899 | n/a |
| GPU frame envelope | 4.899 | 4.899–4.899 | n/a |
| GPU bakeSunShadowMap | 0.093 | 0.093–0.093 | n/a |
| GPU canvasClear | 0.059 | 0.059–0.059 | n/a |
| GPU computeLightVolume | 2.611 | 2.611–2.611 | n/a |
| GPU computeSunShadow | 0.071 | 0.071–0.071 | n/a |
| GPU computeVoxelAO | 0.058 | 0.058–0.058 | n/a |
| GPU fbToScreen | 0.124 | 0.124–0.124 | n/a |
| GPU fogToTrixel | 0.032 | 0.032–0.032 | n/a |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 | n/a |
| GPU textToTrixel | 1.388 | 1.388–1.388 | n/a |
| GPU trixelToFb | 1.214 | 1.214–1.214 | n/a |
| GPU voxelCardinalElect | 0.468 | 0.468–0.468 | n/a |
| GPU voxelCompact | 0.329 | 0.329–0.329 | n/a |
| GPU voxelStage1 | 0.573 | 0.573–0.573 | n/a |
| GPU voxelStage2 | 0.464 | 0.464–0.464 | n/a |
| GPU voxelSunFaces | 1.099 | 1.099–1.099 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.780 | 26.518 | 27.900 | 27.900 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.50 | 1.50–1.50 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
