| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 25.230 | 25.040–25.420 |
| frame p95 | 19.515 | 18.940–20.090 |
| frame p99 | 55.860 | 55.010–56.710 |
| steady frame avg | 16.875 | 16.850–16.900 |
| steady frame p95 | 17.935 | 17.650–18.220 |
| steady frame p99 | 19.935 | 18.580–21.290 |
| GPU frame commandBufferSpans | 13.622 | 13.524–13.721 |
| GPU frame envelope | 13.622 | 13.524–13.721 |
| GPU canvasClear | 0.170 | 0.169–0.170 |
| GPU computeLightVolume | 6.702 | 6.659–6.745 |
| GPU computeSunShadow | 0.232 | 0.224–0.240 |
| GPU computeVoxelAO | 0.454 | 0.454–0.454 |
| GPU computeVoxelAoPerAxis | 3.884 | 3.852–3.916 |
| GPU fbToScreen | 0.044 | 0.044–0.045 |
| GPU fogOverflow | 4.079 | 4.037–4.120 |
| GPU fogPerAxis | 0.023 | 0.023–0.023 |
| GPU fogToTrixel | 0.104 | 0.103–0.106 |
| GPU lightingOverflow | 3.987 | 3.968–4.006 |
| GPU lightingPerAxis | 0.035 | 0.034–0.036 |
| GPU lightingToTrixel | 0.123 | 0.123–0.124 |
| GPU perAxisCellCompact | 0.106 | 0.105–0.107 |
| GPU perAxisScatter | 1.676 | 1.671–1.681 |
| GPU resolvePerAxisScreenDepth | 0.488 | 0.486–0.491 |
| GPU trixelToFb | 0.070 | 0.069–0.070 |
| GPU voxelCompact | 0.070 | 0.069–0.071 |
| GPU voxelPerAxisFinalize | 0.536 | 0.518–0.554 |
| GPU voxelPerAxisOverflow | 2.657 | 2.654–2.660 |
| GPU voxelPerAxisStore | 0.728 | 0.717–0.739 |
| GPU voxelSunFaces | 0.675 | 0.670–0.679 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 30 of each run excluded | 180 | 16.878 | 17.983 | 19.496 | 21.287 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.10 | 1.10–1.10 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
| 2 | True | 120 / 120 | 0 | 120 | 45.000 (0.000) | 745398 / 0 (120) |
