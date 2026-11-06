| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 81.430 | 81.430–81.430 | n/a |
| frame p95 | 82.740 | 82.740–82.740 | n/a |
| frame p99 | 84.860 | 84.860–84.860 | n/a |
| steady frame avg | 81.230 | 81.230–81.230 | n/a |
| steady frame p95 | 82.720 | 82.720–82.720 | n/a |
| steady frame p99 | 84.390 | 84.390–84.390 | n/a |
| GPU frame commandBufferSpans | 44.142 | 44.142–44.142 | n/a |
| GPU frame envelope | 68.867 | 68.867–68.867 | n/a |
| GPU bakeSunShadowMap | 0.854 | 0.854–0.854 | n/a |
| GPU buildLightOcclusionGrid | 1.161 | 1.161–1.161 | n/a |
| GPU computeLightVolume | 0.164 | 0.164–0.164 | n/a |
| GPU computeSunShadow | 0.176 | 0.176–0.176 | n/a |
| GPU entityCanvasToFb | 8.862 | 8.862–8.862 | n/a |
| GPU fbToScreen | 0.182 | 0.182–0.182 | n/a |
| GPU resolvePerAxisScreenDepth | 4.392 | 4.392–4.392 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 90 of each run excluded | 270 | 81.231 | 82.718 | 84.389 | 84.860 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 4.90 | 4.90–4.90 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 360 / 360 | 0 | 5400 | 0.000 (0.000) | 0 / 0 (0) |
