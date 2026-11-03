| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 11.180 | 11.180–11.180 |
| frame p95 | 18.190 | 18.190–18.190 |
| frame p99 | 34.220 | 34.220–34.220 |
| steady frame avg | 10.950 | 10.950–10.950 |
| steady frame p95 | 17.890 | 17.890–17.890 |
| steady frame p99 | 26.280 | 26.280–26.280 |
| GPU frame commandBufferSpans | 6.300 | 6.300–6.300 |
| GPU frame envelope | 7.167 | 7.167–7.167 |
| GPU canvasClear | 0.016 | 0.016–0.016 |
| GPU computeLightVolume | 1.638 | 1.638–1.638 |
| GPU computeSunShadow | 0.071 | 0.071–0.071 |
| GPU computeVoxelAO | 0.037 | 0.037–0.037 |
| GPU fbToScreen | 0.039 | 0.039–0.039 |
| GPU lightingToTrixel | 0.044 | 0.044–0.044 |
| GPU shapeCastBoxes | 0.597 | 0.597–0.597 |
| GPU shapeDepth | 1.771 | 1.771–1.771 |
| GPU shapeOwnerClear | 0.009 | 0.009–0.009 |
| GPU shapeOwnerElect | 1.719 | 1.719–1.719 |
| GPU shapePublish | 1.703 | 1.703–1.703 |
| GPU trixelToFb | 0.157 | 0.157–0.157 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 68 of each run excluded | 207 | 10.955 | 17.885 | 26.282 | 135.875 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 275 / 275 | 0 | 277 | 0.000 (0.000) | 0 / 0 (0) |
