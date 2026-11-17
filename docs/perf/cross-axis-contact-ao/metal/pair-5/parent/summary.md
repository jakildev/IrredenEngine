| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.450 | 11.450–11.450 | n/a |
| frame p95 | 12.330 | 12.330–12.330 | n/a |
| frame p99 | 13.170 | 13.170–13.170 | n/a |
| steady frame avg | 11.490 | 11.490–11.490 | n/a |
| steady frame p95 | 12.560 | 12.560–12.560 | n/a |
| steady frame p99 | 13.170 | 13.170–13.170 | n/a |
| frame split INPUT | 0.003 | 0.003–0.003 | n/a |
| frame split UPDATE | 0.017 | 0.017–0.017 | n/a |
| frame split RENDER | 0.978 | 0.978–0.978 | n/a |
| frame split GPU | 10.195 | 10.195–10.195 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.253 | 0.253–0.253 | n/a |
| GPU frame commandBufferSpans | 10.195 | 10.195–10.195 | n/a |
| GPU frame envelope | 10.195 | 10.195–10.195 | n/a |
| GPU exclusive stage sum | 10.269 | 10.269–10.269 | n/a |
| GPU bakeSunShadowMap | 0.106 | 0.106–0.106 | n/a |
| GPU canvasClear | 0.047 | 0.047–0.047 | n/a |
| GPU computeLightVolume | 0.244 | 0.244–0.244 | n/a |
| GPU computeSunShadow | 0.557 | 0.557–0.557 | n/a |
| GPU computeVoxelAO | 0.015 | 0.015–0.015 | n/a |
| GPU computeVoxelAoPerAxis | 0.029 | 0.029–0.029 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.116 | 0.116–0.116 | n/a |
| GPU lightingPerAxis | 0.036 | 0.036–0.036 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.078 | 0.078–0.078 | n/a |
| GPU perAxisScatter | 0.651 | 0.651–0.651 | n/a |
| GPU trixelToFb | 0.854 | 0.854–0.854 | n/a |
| GPU voxelCompact | 0.098 | 0.098–0.098 | n/a |
| GPU voxelPerAxisFinalize | 1.363 | 1.363–1.363 | n/a |
| GPU voxelPerAxisOverflow | 2.436 | 2.436–2.436 | n/a |
| GPU voxelPerAxisStore | 0.748 | 0.748–0.748 | n/a |
| GPU voxelSunFaces | 2.801 | 2.801–2.801 | n/a |
| input_to_present mean | 14.898 | 14.898–14.898 | n/a |
| input_to_present p99 | 24.250 | 24.250–24.250 | n/a |
| cursor_to_present mean | 11.324 | 11.324–11.324 | n/a |
| cursor_to_present p99 | 13.027 | 13.027–13.027 | n/a |
| frame_start_to_submit mean | 1.076 | 1.076–1.076 | n/a |
| frame_start_to_submit p99 | 2.098 | 2.098–2.098 | n/a |
| submit_to_present mean | 10.291 | 10.291–10.291 | n/a |
| submit_to_present p99 | 11.169 | 11.169–11.169 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.487 | 12.557 | 13.175 | 15.355 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 51719 / 0 (300) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0016 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 57.000 | 57.000–57.000 | 57 | 17100.0 |
| bufferStagedBytes | 16412.000 | 16412.000–16412.000 | 16412 | 4923600.0 |
| bufferFullReplacements | 27.000 | 27.000–27.000 | 27 | 8100.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 735.8 | 735.8–735.8 |
