| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.520 | 9.520–9.520 |
| frame p95 | 8.600 | 8.600–8.600 |
| frame p99 | 82.590 | 82.590–82.590 |
| steady frame avg | 9.550 | 9.550–9.550 |
| steady frame p95 | 8.360 | 8.360–8.360 |
| steady frame p99 | 8.980 | 8.980–8.980 |
| GPU frame commandBufferSpans | 2.301 | 2.301–2.301 |
| GPU frame envelope | 3.187 | 3.187–3.187 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 0.298 | 0.298–0.298 |
| GPU computeSunShadow | 0.062 | 0.062–0.062 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.048 | 0.048–0.048 |
| GPU shapeCastBoxes | 0.131 | 0.131–0.131 |
| GPU shapeDepth | 0.310 | 0.310–0.310 |
| GPU shapeOwnerClear | 0.027 | 0.027–0.027 |
| GPU shapeOwnerElect | 0.325 | 0.325–0.325 |
| GPU shapePublish | 0.326 | 0.326–0.326 |
| GPU trixelToFb | 0.126 | 0.126–0.126 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.548 | 8.356 | 8.975 | 136.432 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
