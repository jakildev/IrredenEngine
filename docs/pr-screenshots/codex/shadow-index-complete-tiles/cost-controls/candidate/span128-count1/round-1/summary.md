| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.330 | 9.330–9.330 |
| frame p95 | 8.840 | 8.840–8.840 |
| frame p99 | 29.640 | 29.640–29.640 |
| steady frame avg | 9.570 | 9.570–9.570 |
| steady frame p95 | 8.840 | 8.840–8.840 |
| steady frame p99 | 9.450 | 9.450–9.450 |
| GPU frame commandBufferSpans | 3.040 | 3.040–3.040 |
| GPU frame envelope | 3.929 | 3.929–3.929 |
| GPU canvasClear | 0.041 | 0.041–0.041 |
| GPU computeLightVolume | 0.566 | 0.566–0.566 |
| GPU computeSunShadow | 0.069 | 0.069–0.069 |
| GPU computeVoxelAO | 0.047 | 0.047–0.047 |
| GPU fbToScreen | 0.121 | 0.121–0.121 |
| GPU lightingToTrixel | 0.051 | 0.051–0.051 |
| GPU shapeCastBoxes | 0.146 | 0.146–0.146 |
| GPU shapeDepth | 0.609 | 0.609–0.609 |
| GPU shapeOwnerClear | 0.024 | 0.024–0.024 |
| GPU shapeOwnerElect | 0.602 | 0.602–0.602 |
| GPU shapePublish | 0.635 | 0.635–0.635 |
| GPU trixelToFb | 0.129 | 0.129–0.129 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.566 | 8.840 | 9.448 | 140.065 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
