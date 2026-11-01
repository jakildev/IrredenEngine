| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 32.500 | 32.500–32.500 | n/a |
| frame p95 | 24.250 | 24.250–24.250 | n/a |
| frame p99 | 645.290 | 645.290–645.290 | n/a |
| steady frame avg | 22.120 | 22.120–22.120 | n/a |
| steady frame p95 | 24.100 | 24.100–24.100 | n/a |
| steady frame p99 | 24.250 | 24.250–24.250 | n/a |
| GPU frame commandBufferSpans | 5.454 | 5.454–5.454 | n/a |
| GPU frame envelope | 5.454 | 5.454–5.454 | n/a |
| GPU bakeSunShadowMap | 0.120 | 0.120–0.120 | n/a |
| GPU canvasClear | 0.127 | 0.127–0.127 | n/a |
| GPU computeLightVolume | 2.846 | 2.846–2.846 | n/a |
| GPU computeSunShadow | 0.132 | 0.132–0.132 | n/a |
| GPU computeVoxelAO | 0.058 | 0.058–0.058 | n/a |
| GPU fbToScreen | 0.126 | 0.126–0.126 | n/a |
| GPU fogToTrixel | 0.034 | 0.034–0.034 | n/a |
| GPU lightingToTrixel | 0.063 | 0.063–0.063 | n/a |
| GPU textToTrixel | 1.646 | 1.646–1.646 | n/a |
| GPU trixelToFb | 1.243 | 1.243–1.243 | n/a |
| GPU voxelCardinalElect | 0.470 | 0.470–0.470 | n/a |
| GPU voxelCompact | 0.828 | 0.828–0.828 | n/a |
| GPU voxelStage1 | 0.588 | 0.588–0.588 | n/a |
| GPU voxelStage2 | 0.468 | 0.468–0.468 | n/a |
| GPU voxelSunFaces | 1.247 | 1.247–1.247 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 22.123 | 24.098 | 24.249 | 24.249 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.40 | 1.40–1.40 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
