| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.430 | 9.430–9.430 |
| frame p95 | 8.940 | 8.940–8.940 |
| frame p99 | 69.270 | 69.270–69.270 |
| steady frame avg | 9.520 | 9.520–9.520 |
| steady frame p95 | 8.880 | 8.880–8.880 |
| steady frame p99 | 10.450 | 10.450–10.450 |
| GPU frame commandBufferSpans | 4.694 | 4.694–4.694 |
| GPU frame envelope | 5.628 | 5.628–5.628 |
| GPU canvasClear | 0.049 | 0.049–0.049 |
| GPU computeLightVolume | 2.045 | 2.045–2.045 |
| GPU computeSunShadow | 0.394 | 0.394–0.394 |
| GPU computeVoxelAO | 0.870 | 0.870–0.870 |
| GPU fbToScreen | 0.172 | 0.172–0.172 |
| GPU lightingToTrixel | 0.173 | 0.173–0.173 |
| GPU shapeCastBoxes | 0.312 | 0.312–0.312 |
| GPU shapeDepth | 0.315 | 0.315–0.315 |
| GPU shapeOwnerClear | 0.025 | 0.025–0.025 |
| GPU shapeOwnerElect | 0.536 | 0.536–0.536 |
| GPU shapePublish | 1.301 | 1.301–1.301 |
| GPU trixelToFb | 0.123 | 0.123–0.123 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.518 | 8.880 | 10.452 | 142.899 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
