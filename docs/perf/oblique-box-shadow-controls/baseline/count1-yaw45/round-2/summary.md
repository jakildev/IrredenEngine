| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.390 | 9.390–9.390 |
| frame p95 | 9.090 | 9.090–9.090 |
| frame p99 | 51.470 | 51.470–51.470 |
| steady frame avg | 9.570 | 9.570–9.570 |
| steady frame p95 | 8.780 | 8.780–8.780 |
| steady frame p99 | 19.390 | 19.390–19.390 |
| GPU frame commandBufferSpans | 4.598 | 4.598–4.598 |
| GPU frame envelope | 5.523 | 5.523–5.523 |
| GPU canvasClear | 0.045 | 0.045–0.045 |
| GPU computeLightVolume | 1.752 | 1.752–1.752 |
| GPU computeSunShadow | 0.526 | 0.526–0.526 |
| GPU computeVoxelAO | 1.009 | 1.009–1.009 |
| GPU fbToScreen | 0.162 | 0.162–0.162 |
| GPU lightingToTrixel | 0.165 | 0.165–0.165 |
| GPU shapeCastBoxes | 0.268 | 0.268–0.268 |
| GPU shapeDepth | 0.316 | 0.316–0.316 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.484 | 0.484–0.484 |
| GPU shapePublish | 1.093 | 1.093–1.093 |
| GPU trixelToFb | 0.123 | 0.123–0.123 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.569 | 8.782 | 19.395 | 140.729 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
