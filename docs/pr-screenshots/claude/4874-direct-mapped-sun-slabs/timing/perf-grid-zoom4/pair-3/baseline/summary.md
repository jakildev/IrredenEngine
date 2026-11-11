| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 38.760 | 38.760–38.760 | n/a |
| frame p95 | 40.520 | 40.520–40.520 | n/a |
| frame p99 | 682.000 | 682.000–682.000 | n/a |
| steady frame avg | 29.190 | 29.190–29.190 | n/a |
| steady frame p95 | 36.400 | 36.400–36.400 | n/a |
| steady frame p99 | 40.520 | 40.520–40.520 | n/a |
| GPU frame commandBufferSpans | 7.426 | 7.426–7.426 | n/a |
| GPU frame envelope | 7.426 | 7.426–7.426 | n/a |
| GPU bakeSunShadowMap | 0.121 | 0.121–0.121 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 5.990 | 5.990–5.990 | n/a |
| GPU computeSunShadow | 0.070 | 0.070–0.070 | n/a |
| GPU computeVoxelAO | 0.049 | 0.049–0.049 | n/a |
| GPU fbToScreen | 0.034 | 0.034–0.034 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.040 | 0.040–0.040 | n/a |
| GPU textToTrixel | 0.990 | 0.990–0.990 | n/a |
| GPU trixelToFb | 0.788 | 0.788–0.788 | n/a |
| GPU voxelCardinalElect | 1.811 | 1.811–1.811 | n/a |
| GPU voxelCompact | 0.777 | 0.777–0.777 | n/a |
| GPU voxelStage1 | 1.903 | 1.903–1.903 | n/a |
| GPU voxelStage2 | 1.760 | 1.760–1.760 | n/a |
| GPU voxelSunFaces | 1.008 | 1.008–1.008 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 29.188 | 36.397 | 40.516 | 40.516 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.80 | 1.80–1.80 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
