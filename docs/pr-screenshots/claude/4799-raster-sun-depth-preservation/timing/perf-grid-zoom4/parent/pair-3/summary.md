| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 35.840 | 35.840–35.840 | n/a |
| frame p95 | 35.020 | 35.020–35.020 | n/a |
| frame p99 | 680.730 | 680.730–680.730 | n/a |
| steady frame avg | 25.460 | 25.460–25.460 | n/a |
| steady frame p95 | 34.090 | 34.090–34.090 | n/a |
| steady frame p99 | 35.020 | 35.020–35.020 | n/a |
| GPU frame commandBufferSpans | 7.608 | 7.608–7.608 | n/a |
| GPU frame envelope | 7.608 | 7.608–7.608 | n/a |
| GPU bakeSunShadowMap | 0.037 | 0.037–0.037 | n/a |
| GPU canvasClear | 0.013 | 0.013–0.013 | n/a |
| GPU computeLightVolume | 6.197 | 6.197–6.197 | n/a |
| GPU computeSunShadow | 0.053 | 0.053–0.053 | n/a |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 | n/a |
| GPU fbToScreen | 0.037 | 0.037–0.037 | n/a |
| GPU fogToTrixel | 0.016 | 0.016–0.016 | n/a |
| GPU lightingToTrixel | 0.043 | 0.043–0.043 | n/a |
| GPU textToTrixel | 0.862 | 0.862–0.862 | n/a |
| GPU trixelToFb | 1.001 | 1.001–1.001 | n/a |
| GPU voxelCardinalElect | 1.903 | 1.903–1.903 | n/a |
| GPU voxelCompact | 0.220 | 0.220–0.220 | n/a |
| GPU voxelStage1 | 2.003 | 2.003–2.003 | n/a |
| GPU voxelStage2 | 1.874 | 1.874–1.874 | n/a |
| GPU voxelSunFaces | 1.010 | 1.010–1.010 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 25.456 | 34.091 | 35.015 | 35.015 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
