| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 18.690 | 18.690–18.690 |
| frame p95 | 20.930 | 20.930–20.930 |
| frame p99 | 22.520 | 22.520–22.520 |
| steady frame avg | 18.420 | 18.420–18.420 |
| steady frame p95 | 20.930 | 20.930–20.930 |
| steady frame p99 | 22.120 | 22.120–22.120 |
| GPU frame commandBufferSpans | 4.354 | 4.354–4.354 |
| GPU frame envelope | 4.354 | 4.354–4.354 |
| GPU canvasClear | 0.044 | 0.044–0.044 |
| GPU computeLightVolume | 2.400 | 2.400–2.400 |
| GPU computeSunShadow | 0.057 | 0.057–0.057 |
| GPU computeVoxelAO | 0.053 | 0.053–0.053 |
| GPU fbToScreen | 0.118 | 0.118–0.118 |
| GPU fogToTrixel | 0.029 | 0.029–0.029 |
| GPU lightingToTrixel | 0.047 | 0.047–0.047 |
| GPU textToTrixel | 1.041 | 1.041–1.041 |
| GPU trixelToFb | 0.237 | 0.237–0.237 |
| GPU voxelCardinalElect | 0.466 | 0.466–0.466 |
| GPU voxelCompact | 0.400 | 0.400–0.400 |
| GPU voxelStage1 | 0.542 | 0.542–0.542 |
| GPU voxelStage2 | 0.461 | 0.461–0.461 |
| GPU voxelSunFaces | 0.999 | 0.999–0.999 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 18.423 | 20.925 | 22.121 | 22.521 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 3 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |
