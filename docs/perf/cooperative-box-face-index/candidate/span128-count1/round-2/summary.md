| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 9.600 | 9.600–9.600 |
| frame p95 | 9.660 | 9.660–9.660 |
| frame p99 | 48.720 | 48.720–48.720 |
| steady frame avg | 9.860 | 9.860–9.860 |
| steady frame p95 | 9.550 | 9.550–9.550 |
| steady frame p99 | 24.540 | 24.540–24.540 |
| GPU frame commandBufferSpans | 4.225 | 4.225–4.225 |
| GPU frame envelope | 5.276 | 5.276–5.276 |
| GPU canvasClear | 0.359 | 0.359–0.359 |
| GPU computeLightVolume | 0.991 | 0.991–0.991 |
| GPU computeSunShadow | 0.087 | 0.087–0.087 |
| GPU computeVoxelAO | 0.101 | 0.101–0.101 |
| GPU fbToScreen | 0.110 | 0.110–0.110 |
| GPU lightingToTrixel | 0.054 | 0.054–0.054 |
| GPU shapeCastBoxes | 0.111 | 0.111–0.111 |
| GPU shapeDepth | 0.570 | 0.570–0.570 |
| GPU shapeOwnerClear | 0.125 | 0.125–0.125 |
| GPU shapeOwnerElect | 0.752 | 0.752–0.752 |
| GPU shapePublish | 0.687 | 0.687–0.687 |
| GPU trixelToFb | 0.489 | 0.489–0.489 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 9.860 | 9.547 | 24.536 | 153.525 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
