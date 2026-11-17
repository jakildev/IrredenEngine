| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 11.300 | 11.300–11.300 | n/a |
| frame p95 | 11.600 | 11.600–11.600 | n/a |
| frame p99 | 11.700 | 11.700–11.700 | n/a |
| steady frame avg | 11.290 | 11.290–11.290 | n/a |
| steady frame p95 | 11.600 | 11.600–11.600 | n/a |
| steady frame p99 | 11.730 | 11.730–11.730 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 0.014 | 0.014–0.014 | n/a |
| frame split RENDER | 0.807 | 0.807–0.807 | n/a |
| frame split GPU | 10.251 | 10.251–10.251 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 0.228 | 0.228–0.228 | n/a |
| GPU frame commandBufferSpans | 10.251 | 10.251–10.251 | n/a |
| GPU frame envelope | 10.251 | 10.251–10.251 | n/a |
| GPU exclusive stage sum | 10.359 | 10.359–10.359 | n/a |
| GPU bakeSunShadowMap | 0.108 | 0.108–0.108 | n/a |
| GPU canvasClear | 0.048 | 0.048–0.048 | n/a |
| GPU computeLightVolume | 0.248 | 0.248–0.248 | n/a |
| GPU computeSunShadow | 0.560 | 0.560–0.560 | n/a |
| GPU computeVoxelAO | 0.014 | 0.014–0.014 | n/a |
| GPU computeVoxelAoPerAxis | 0.045 | 0.045–0.045 | n/a |
| GPU fbToScreen | 0.031 | 0.031–0.031 | n/a |
| GPU fogOverflow | 0.009 | 0.009–0.009 | n/a |
| GPU fogPerAxis | 0.024 | 0.024–0.024 | n/a |
| GPU fogToTrixel | 0.012 | 0.012–0.012 | n/a |
| GPU lightingOverflow | 0.121 | 0.121–0.121 | n/a |
| GPU lightingPerAxis | 0.037 | 0.037–0.037 | n/a |
| GPU lightingToTrixel | 0.015 | 0.015–0.015 | n/a |
| GPU perAxisCellCompact | 0.079 | 0.079–0.079 | n/a |
| GPU perAxisScatter | 0.649 | 0.649–0.649 | n/a |
| GPU trixelToFb | 0.913 | 0.913–0.913 | n/a |
| GPU voxelCompact | 0.100 | 0.100–0.100 | n/a |
| GPU voxelPerAxisFinalize | 1.367 | 1.367–1.367 | n/a |
| GPU voxelPerAxisOverflow | 2.434 | 2.434–2.434 | n/a |
| GPU voxelPerAxisStore | 0.745 | 0.745–0.745 | n/a |
| GPU voxelSunFaces | 2.800 | 2.800–2.800 | n/a |
| input_to_present mean | 14.798 | 14.798–14.798 | n/a |
| input_to_present p99 | 22.937 | 22.937–22.937 | n/a |
| cursor_to_present mean | 11.128 | 11.128–11.128 | n/a |
| cursor_to_present p99 | 11.550 | 11.550–11.550 | n/a |
| frame_start_to_submit mean | 0.856 | 0.856–0.856 | n/a |
| frame_start_to_submit p99 | 1.207 | 1.207–1.207 | n/a |
| submit_to_present mean | 10.301 | 10.301–10.301 | n/a |
| submit_to_present p99 | 10.775 | 10.775–10.775 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 226 | 11.295 | 11.604 | 11.734 | 11.792 |

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
