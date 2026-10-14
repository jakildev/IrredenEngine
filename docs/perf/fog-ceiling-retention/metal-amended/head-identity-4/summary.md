| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.020 | 21.020–21.020 |
| frame p95 | 23.080 | 23.080–23.080 |
| frame p99 | 23.560 | 23.560–23.560 |
| steady frame avg | 20.830 | 20.830–20.830 |
| steady frame p95 | 22.990 | 22.990–22.990 |
| steady frame p99 | 23.120 | 23.120–23.120 |
| GPU frame commandBufferSpans | 4.595 | 4.595–4.595 |
| GPU frame envelope | 4.595 | 4.595–4.595 |
| GPU canvasClear | 0.053 | 0.053–0.053 |
| GPU computeLightVolume | 2.574 | 2.574–2.574 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.056 | 0.056–0.056 |
| GPU fbToScreen | 0.126 | 0.126–0.126 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.056 | 0.056–0.056 |
| GPU textToTrixel | 0.425 | 0.425–0.425 |
| GPU trixelToFb | 0.544 | 0.544–0.544 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.447 | 0.447–0.447 |
| GPU voxelStage1 | 0.588 | 0.588–0.588 |
| GPU voxelStage2 | 0.478 | 0.478–0.478 |
| GPU voxelSunFaces | 1.019 | 1.019–1.019 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 20.828 | 22.990 | 23.124 | 23.707 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
