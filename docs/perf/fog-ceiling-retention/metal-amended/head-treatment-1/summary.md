| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 21.280 | 21.280–21.280 |
| frame p95 | 23.260 | 23.260–23.260 |
| frame p99 | 23.500 | 23.500–23.500 |
| steady frame avg | 21.380 | 21.380–21.380 |
| steady frame p95 | 23.260 | 23.260–23.260 |
| steady frame p99 | 23.490 | 23.490–23.490 |
| GPU frame commandBufferSpans | 4.602 | 4.602–4.602 |
| GPU frame envelope | 4.602 | 4.602–4.602 |
| GPU canvasClear | 0.058 | 0.058–0.058 |
| GPU computeLightVolume | 2.579 | 2.579–2.579 |
| GPU computeSunShadow | 0.064 | 0.064–0.064 |
| GPU computeVoxelAO | 0.057 | 0.057–0.057 |
| GPU fbToScreen | 0.125 | 0.125–0.125 |
| GPU fogToTrixel | 0.034 | 0.034–0.034 |
| GPU lightingToTrixel | 0.057 | 0.057–0.057 |
| GPU textToTrixel | 0.424 | 0.424–0.424 |
| GPU trixelToFb | 0.534 | 0.534–0.534 |
| GPU voxelCardinalElect | 0.483 | 0.483–0.483 |
| GPU voxelCompact | 0.430 | 0.430–0.430 |
| GPU voxelStage1 | 0.588 | 0.588–0.588 |
| GPU voxelStage2 | 0.478 | 0.478–0.478 |
| GPU voxelSunFaces | 1.019 | 1.019–1.019 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 21.380 | 23.262 | 23.494 | 23.497 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.30 | 1.30–1.30 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
