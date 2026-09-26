| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.530 | 9.530–9.530 |
| frame p95 | 9.390 | 9.390–9.390 |
| frame p99 | 38.840 | 38.840–38.840 |
| steady frame avg | 9.760 | 9.760–9.760 |
| steady frame p95 | 9.090 | 9.090–9.090 |
| steady frame p99 | 12.150 | 12.150–12.150 |
| GPU frame commandBufferSpans | 3.762 | 3.762–3.762 |
| GPU frame envelope | 4.831 | 4.831–4.831 |
| GPU canvasClear | 0.128 | 0.128–0.128 |
| GPU computeLightVolume | 0.533 | 0.533–0.533 |
| GPU computeSunShadow | 0.041 | 0.041–0.041 |
| GPU computeVoxelAO | 0.059 | 0.059–0.059 |
| GPU fbToScreen | 0.079 | 0.079–0.079 |
| GPU lightingToTrixel | 0.032 | 0.032–0.032 |
| GPU shapeCastBoxes | 1.001 | 1.001–1.001 |
| GPU shapeDepth | 0.364 | 0.364–0.364 |
| GPU shapeOwnerClear | 0.057 | 0.057–0.057 |
| GPU shapeOwnerElect | 0.413 | 0.413–0.413 |
| GPU shapePublish | 0.435 | 0.435–0.435 |
| GPU trixelToFb | 0.380 | 0.380–0.380 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.760 | 9.087 | 12.151 | 156.952 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
