| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.410 | 9.410–9.410 |
| frame p95 | 10.410 | 10.410–10.410 |
| frame p99 | 44.460 | 44.460–44.460 |
| steady frame avg | 9.620 | 9.620–9.620 |
| steady frame p95 | 10.700 | 10.700–10.700 |
| steady frame p99 | 16.670 | 16.670–16.670 |
| GPU frame commandBufferSpans | 4.111 | 4.111–4.111 |
| GPU frame envelope | 5.053 | 5.053–5.053 |
| GPU canvasClear | 0.050 | 0.050–0.050 |
| GPU computeLightVolume | 1.472 | 1.472–1.472 |
| GPU computeSunShadow | 0.427 | 0.427–0.427 |
| GPU computeVoxelAO | 0.830 | 0.830–0.830 |
| GPU fbToScreen | 0.177 | 0.177–0.177 |
| GPU lightingToTrixel | 0.162 | 0.162–0.162 |
| GPU shapeCastBoxes | 0.226 | 0.226–0.226 |
| GPU shapeDepth | 0.290 | 0.290–0.290 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.442 | 0.442–0.442 |
| GPU shapePublish | 0.955 | 0.955–0.955 |
| GPU trixelToFb | 0.119 | 0.119–0.119 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.622 | 10.704 | 16.670 | 142.437 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
