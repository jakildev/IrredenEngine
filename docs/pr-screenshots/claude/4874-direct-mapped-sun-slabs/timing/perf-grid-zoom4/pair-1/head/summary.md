| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 37.660 | 37.660–37.660 | n/a |
| frame p95 | 36.520 | 36.520–36.520 | n/a |
| frame p99 | 692.230 | 692.230–692.230 | n/a |
| steady frame avg | 27.380 | 27.380–27.380 | n/a |
| steady frame p95 | 35.270 | 35.270–35.270 | n/a |
| steady frame p99 | 36.610 | 36.610–36.610 | n/a |
| GPU frame commandBufferSpans | 7.673 | 7.673–7.673 | n/a |
| GPU frame envelope | 7.673 | 7.673–7.673 | n/a |
| GPU bakeSunShadowMap | 0.122 | 0.122–0.122 | n/a |
| GPU canvasClear | 0.044 | 0.044–0.044 | n/a |
| GPU computeLightVolume | 6.131 | 6.131–6.131 | n/a |
| GPU computeSunShadow | 0.059 | 0.059–0.059 | n/a |
| GPU computeVoxelAO | 0.050 | 0.050–0.050 | n/a |
| GPU fbToScreen | 0.035 | 0.035–0.035 | n/a |
| GPU fogToTrixel | 0.014 | 0.014–0.014 | n/a |
| GPU lightingToTrixel | 0.042 | 0.042–0.042 | n/a |
| GPU textToTrixel | 1.113 | 1.113–1.113 | n/a |
| GPU trixelToFb | 0.408 | 0.408–0.408 | n/a |
| GPU voxelCardinalElect | 1.832 | 1.832–1.832 | n/a |
| GPU voxelCompact | 0.292 | 0.292–0.292 | n/a |
| GPU voxelStage1 | 1.870 | 1.870–1.870 | n/a |
| GPU voxelStage2 | 1.805 | 1.805–1.805 | n/a |
| GPU voxelSunFaces | 0.987 | 0.987–0.987 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 15 of each run excluded | 45 | 27.377 | 35.271 | 36.612 | 36.612 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 60 / 60 | 0 | 60 | 0.000 (0.000) | 0 / 0 (0) |
