| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.480 | 9.480–9.480 |
| frame p95 | 8.750 | 8.750–8.750 |
| frame p99 | 72.010 | 72.010–72.010 |
| steady frame avg | 9.590 | 9.590–9.590 |
| steady frame p95 | 8.720 | 8.720–8.720 |
| steady frame p99 | 18.040 | 18.040–18.040 |
| GPU frame commandBufferSpans | 4.455 | 4.455–4.455 |
| GPU frame envelope | 5.366 | 5.366–5.366 |
| GPU canvasClear | 0.032 | 0.032–0.032 |
| GPU computeLightVolume | 1.317 | 1.317–1.317 |
| GPU computeSunShadow | 0.031 | 0.031–0.031 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.072 | 0.072–0.072 |
| GPU lightingToTrixel | 0.028 | 0.028–0.028 |
| GPU shapeCastBoxes | 0.502 | 0.502–0.502 |
| GPU shapeDepth | 0.862 | 0.862–0.862 |
| GPU shapeOwnerClear | 0.017 | 0.017–0.017 |
| GPU shapeOwnerElect | 1.504 | 1.504–1.504 |
| GPU shapePublish | 1.008 | 1.008–1.008 |
| GPU trixelToFb | 0.088 | 0.088–0.088 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.591 | 8.718 | 18.040 | 137.906 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
