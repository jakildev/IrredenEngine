| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 36.990 | 36.990–36.990 | n/a |
| frame p95 | 47.780 | 47.780–47.780 | n/a |
| frame p99 | 746.470 | 746.470–746.470 | n/a |
| steady frame avg | 26.880 | 26.880–26.880 | n/a |
| steady frame p95 | 32.180 | 32.180–32.180 | n/a |
| steady frame p99 | 140.100 | 140.100–140.100 | n/a |
| GPU frame commandBufferSpans | 5.541 | 5.541–5.541 | n/a |
| GPU frame envelope | 5.541 | 5.541–5.541 | n/a |
| GPU bakeSunShadowMap | 0.087 | 0.087–0.087 | n/a |
| GPU canvasClear | 0.095 | 0.095–0.095 | n/a |
| GPU computeLightVolume | 2.554 | 2.554–2.554 | n/a |
| GPU computeSunShadow | 0.074 | 0.074–0.074 | n/a |
| GPU computeVoxelAO | 0.054 | 0.054–0.054 | n/a |
| GPU fbToScreen | 0.117 | 0.117–0.117 | n/a |
| GPU fogToTrixel | 0.031 | 0.031–0.031 | n/a |
| GPU lightingToTrixel | 0.053 | 0.053–0.053 | n/a |
| GPU textToTrixel | 1.183 | 1.183–1.183 | n/a |
| GPU trixelToFb | 1.096 | 1.096–1.096 | n/a |
| GPU voxelCardinalElect | 0.451 | 0.451–0.451 | n/a |
| GPU voxelCompact | 0.271 | 0.271–0.271 | n/a |
| GPU voxelStage1 | 0.547 | 0.547–0.547 | n/a |
| GPU voxelStage2 | 0.447 | 0.447–0.447 | n/a |
| GPU voxelSunFaces | 1.052 | 1.052–1.052 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 26.885 | 32.183 | 140.096 | 140.096 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.60 | 1.60–1.60 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
