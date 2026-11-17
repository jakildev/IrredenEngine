| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.550 | 11.550–11.550 | n/a |
| frame p95 | 12.350 | 12.350–12.350 | n/a |
| frame p99 | 12.950 | 12.950–12.950 | n/a |
| steady frame avg | 11.490 | 11.490–11.490 | n/a |
| steady frame p95 | 11.860 | 11.860–11.860 | n/a |
| steady frame p99 | 12.510 | 12.510–12.510 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.014 | 0.014–0.014 | n/a |
| frame split RENDER | 1.029 | 1.029–1.029 | n/a |
| frame split GPU | 10.253 | 10.253–10.253 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.249 | 0.249–0.249 | n/a |
| GPU frame commandBufferSpans | 10.253 | 10.253–10.253 | n/a |
| GPU frame envelope | 10.253 | 10.253–10.253 | n/a |
| GPU exclusive stage sum | 10.318 | 10.318–10.318 | n/a |
| GPU bakeSunShadowMap | 0.107 | 0.107–0.107 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 0.246 | 0.246–0.246 | n/a |
| GPU computeSunShadow | 0.555 | 0.555–0.555 | n/a |
| GPU computeVoxelAO | 0.016 | 0.016–0.016 | n/a |
| GPU computeVoxelAoPerAxis | 0.046 | 0.046–0.046 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.121 | 0.121–0.121 | n/a |
| GPU lightingPerAxis | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.080 | 0.080–0.080 | n/a |
| GPU perAxisScatter | 0.648 | 0.648–0.648 | n/a |
| GPU trixelToFb | 0.879 | 0.879–0.879 | n/a |
| GPU voxelCompact | 0.100 | 0.100–0.100 | n/a |
| GPU voxelPerAxisFinalize | 1.366 | 1.366–1.366 | n/a |
| GPU voxelPerAxisOverflow | 2.433 | 2.433–2.433 | n/a |
| GPU voxelPerAxisStore | 0.746 | 0.746–0.746 | n/a |
| GPU voxelSunFaces | 2.801 | 2.801–2.801 | n/a |
| input_to_present mean | 14.901 | 14.901–14.901 | n/a |
| input_to_present p99 | 23.355 | 23.355–23.355 | n/a |
| cursor_to_present mean | 11.325 | 11.325–11.325 | n/a |
| cursor_to_present p99 | 12.376 | 12.376–12.376 | n/a |
| frame_start_to_submit mean | 1.018 | 1.018–1.018 | n/a |
| frame_start_to_submit p99 | 1.960 | 1.960–1.960 | n/a |
| submit_to_present mean | 10.333 | 10.333–10.333 | n/a |
| submit_to_present p99 | 10.775 | 10.775–10.775 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 226 | 11.495 | 11.856 | 12.513 | 12.954 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 301 / 301 | 0 | 301 | 45.000 (0.000) | 51719 / 0 (301) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 226 / 226 | 0 | 0.0005 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17157.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4940012.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8127.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |
