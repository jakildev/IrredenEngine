| Measurement | Mean ms | Run min–max ms |
|---|---:|---:|
| frame avg | 8.700 | 8.700–8.700 |
| frame p95 | 9.080 | 9.080–9.080 |
| frame p99 | 10.350 | 10.350–10.350 |
| steady frame avg | 8.690 | 8.690–8.690 |
| steady frame p95 | 8.990 | 8.990–8.990 |
| steady frame p99 | 10.300 | 10.300–10.300 |
| GPU frame commandBufferSpans | 4.217 | 4.217–4.217 |
| GPU frame envelope | 4.493 | 4.493–4.493 |
| GPU canvasClear | 0.306 | 0.306–0.306 |
| GPU computeLightVolume | 0.076 | 0.076–0.076 |
| GPU computeSunShadow | 0.167 | 0.167–0.167 |
| GPU computeVoxelAO | 0.044 | 0.044–0.044 |
| GPU computeVoxelAoPerAxis | 1.312 | 1.312–1.312 |
| GPU fbToScreen | 0.092 | 0.092–0.092 |
| GPU lightingOverflow | 0.825 | 0.825–0.825 |
| GPU lightingPerAxis | 0.071 | 0.071–0.071 |
| GPU lightingToTrixel | 0.031 | 0.031–0.031 |
| GPU perAxisCellCompact | 0.149 | 0.149–0.149 |
| GPU perAxisScatter | 0.128 | 0.128–0.128 |
| GPU resolvePerAxisScreenDepth | 0.059 | 0.059–0.059 |
| GPU trixelToFb | 0.661 | 0.661–0.661 |
| GPU voxelCompact | 0.084 | 0.084–0.084 |
| GPU voxelPerAxisFinalize | 0.159 | 0.159–0.159 |
| GPU voxelPerAxisOverflow | 0.237 | 0.237–0.237 |
| GPU voxelPerAxisStore | 2.249 | 2.249–2.249 |
| GPU voxelSunFaces | 0.134 | 0.134–0.134 |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 243 of each run excluded | 732 | 8.687 | 8.994 | 10.304 | 155.617 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.00 | 1.00–1.00 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 975 / 975 | 0 | 977 | 22.500 (0.000) | 7 / 0 (975) |
