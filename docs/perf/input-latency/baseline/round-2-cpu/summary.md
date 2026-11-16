| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 14.950 | 14.950–14.950 | n/a |
| frame p95 | 18.600 | 18.600–18.600 | n/a |
| frame p99 | 39.080 | 39.080–39.080 | n/a |
| steady frame avg | 14.150 | 14.150–14.150 | n/a |
| steady frame p95 | 18.260 | 18.260–18.260 | n/a |
| steady frame p99 | 19.660 | 19.660–19.660 | n/a |
| frame split INPUT | 0.002 | 0.002–0.002 | n/a |
| frame split UPDATE | 6.905 | 6.905–6.905 | n/a |
| frame split RENDER | 3.279 | 3.279–3.279 | n/a |
| frame split GPU | 3.711 | 3.711–3.711 | n/a |
| frame split Pacing | 0.265 | 0.265–0.265 | n/a |
| frame split Remainder | 0.786 | 0.786–0.786 | n/a |
| GPU frame commandBufferSpans | 3.711 | 3.711–3.711 | n/a |
| GPU frame envelope | 3.711 | 3.711–3.711 | n/a |
| input_to_present mean | 15.814 | 15.814–15.814 | n/a |
| input_to_present p99 | 23.242 | 23.242–23.242 | n/a |
| cursor_to_present mean | 7.164 | 7.164–7.164 | n/a |
| cursor_to_present p99 | 11.792 | 11.792–11.792 | n/a |
| frame_start_to_submit mean | 9.801 | 9.801–9.801 | n/a |
| frame_start_to_submit p99 | 11.984 | 11.984–11.984 | n/a |
| submit_to_present mean | 3.912 | 3.912–3.912 | n/a |
| submit_to_present p99 | 8.630 | 8.630–8.630 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 14.154 | 18.256 | 19.660 | 21.275 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 0.90 | 0.90–0.90 | 6 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0004 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 0 | 0.0 |
| bufferStagedPatches | 4.000 | 4.000–4.000 | 4 | 1200.0 |
| bufferStagedBytes | 148.000 | 148.000–148.000 | 148 | 44400.0 |
| bufferFullReplacements | 15.000 | 15.000–15.000 | 19 | 4504.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 182.2 | 182.2–182.2 |
