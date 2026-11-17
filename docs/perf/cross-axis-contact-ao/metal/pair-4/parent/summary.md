| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.270 | 11.270–11.270 | n/a |
| frame p95 | 11.530 | 11.530–11.530 | n/a |
| frame p99 | 11.630 | 11.630–11.630 | n/a |
| steady frame avg | 11.280 | 11.280–11.280 | n/a |
| steady frame p95 | 11.520 | 11.520–11.520 | n/a |
| steady frame p99 | 11.600 | 11.600–11.600 | n/a |
| frame split INPUT | 0.001 | 0.001–0.001 | n/a |
| frame split UPDATE | 0.012 | 0.012–0.012 | n/a |
| frame split RENDER | 0.768 | 0.768–0.768 | n/a |
| frame split GPU | 10.258 | 10.258–10.258 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.228 | 0.228–0.228 | n/a |
| GPU frame commandBufferSpans | 10.258 | 10.258–10.258 | n/a |
| GPU frame envelope | 10.258 | 10.258–10.258 | n/a |
| GPU exclusive stage sum | 10.329 | 10.329–10.329 | n/a |
| GPU bakeSunShadowMap | 0.108 | 0.108–0.108 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 0.248 | 0.248–0.248 | n/a |
| GPU computeSunShadow | 0.560 | 0.560–0.560 | n/a |
| GPU computeVoxelAO | 0.014 | 0.014–0.014 | n/a |
| GPU computeVoxelAoPerAxis | 0.032 | 0.032–0.032 | n/a |
| GPU fbToScreen | 0.032 | 0.032–0.032 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.011 | 0.011–0.011 | n/a |
| GPU lightingOverflow | 0.116 | 0.116–0.116 | n/a |
| GPU lightingPerAxis | 0.037 | 0.037–0.037 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.648 | 0.648–0.648 | n/a |
| GPU trixelToFb | 0.896 | 0.896–0.896 | n/a |
| GPU voxelCompact | 0.096 | 0.096–0.096 | n/a |
| GPU voxelPerAxisFinalize | 1.368 | 1.368–1.368 | n/a |
| GPU voxelPerAxisOverflow | 2.437 | 2.437–2.437 | n/a |
| GPU voxelPerAxisStore | 0.749 | 0.749–0.749 | n/a |
| GPU voxelSunFaces | 2.801 | 2.801–2.801 | n/a |
| input_to_present mean | 14.782 | 14.782–14.782 | n/a |
| input_to_present p99 | 22.742 | 22.742–22.742 | n/a |
| cursor_to_present mean | 11.106 | 11.106–11.106 | n/a |
| cursor_to_present p99 | 11.448 | 11.448–11.448 | n/a |
| frame_start_to_submit mean | 0.795 | 0.795–0.795 | n/a |
| frame_start_to_submit p99 | 0.972 | 0.972–0.972 | n/a |
| submit_to_present mean | 10.333 | 10.333–10.333 | n/a |
| submit_to_present p99 | 10.655 | 10.655–10.655 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 11.276 | 11.524 | 11.603 | 11.649 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.70 | 0.70–0.70 | 1 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 45.000 (0.000) | 51719 / 0 (300) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0003 |

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
