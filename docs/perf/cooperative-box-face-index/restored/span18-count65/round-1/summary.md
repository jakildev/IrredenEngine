| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.470 | 9.470–9.470 |
| frame p95 | 9.700 | 9.700–9.700 |
| frame p99 | 40.520 | 40.520–40.520 |
| steady frame avg | 9.700 | 9.700–9.700 |
| steady frame p95 | 9.620 | 9.620–9.620 |
| steady frame p99 | 18.170 | 18.170–18.170 |
| GPU frame commandBufferSpans | 5.415 | 5.415–5.415 |
| GPU frame envelope | 6.415 | 6.415–6.415 |
| GPU canvasClear | 0.051 | 0.051–0.051 |
| GPU computeLightVolume | 1.457 | 1.457–1.457 |
| GPU computeSunShadow | 0.058 | 0.058–0.058 |
| GPU computeVoxelAO | 0.109 | 0.109–0.109 |
| GPU fbToScreen | 0.084 | 0.084–0.084 |
| GPU lightingToTrixel | 0.041 | 0.041–0.041 |
| GPU shapeCastBoxes | 1.593 | 1.593–1.593 |
| GPU shapeDepth | 0.564 | 0.564–0.564 |
| GPU shapeOwnerClear | 0.029 | 0.029–0.029 |
| GPU shapeOwnerElect | 1.408 | 1.408–1.408 |
| GPU shapePublish | 1.060 | 1.060–1.060 |
| GPU trixelToFb | 0.187 | 0.187–0.187 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.701 | 9.624 | 18.172 | 150.430 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
