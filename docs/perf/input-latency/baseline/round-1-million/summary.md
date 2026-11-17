| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 28.720 | 28.720–28.720 | n/a |
| frame p95 | 29.100 | 29.100–29.100 | n/a |
| frame p99 | 30.220 | 30.220–30.220 | n/a |
| steady frame avg | 27.840 | 27.840–27.840 | n/a |
| steady frame p95 | 29.100 | 29.100–29.100 | n/a |
| steady frame p99 | 29.700 | 29.700–29.700 | n/a |
| frame split INPUT | 0.004 | 0.004–0.004 | n/a |
| frame split UPDATE | 3.408 | 3.408–3.408 | n/a |
| frame split RENDER | 5.156 | 5.156–5.156 | n/a |
| frame split GPU | 18.764 | 18.764–18.764 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder | 1.383 | 1.383–1.383 | n/a |
| GPU frame commandBufferSpans | 18.764 | 18.764–18.764 | n/a |
| GPU frame envelope | 18.764 | 18.764–18.764 | n/a |
| input_to_present mean | 26.275 | 26.275–26.275 | n/a |
| input_to_present p99 | 27.194 | 27.194–27.194 | n/a |
| cursor_to_present mean | 24.268 | 24.268–24.268 | n/a |
| cursor_to_present p99 | 24.991 | 24.991–24.991 | n/a |
| frame_start_to_submit mean | 8.579 | 8.579–8.579 | n/a |
| frame_start_to_submit p99 | 10.668 | 10.668–10.668 | n/a |
| submit_to_present mean | 19.123 | 19.123–19.123 | n/a |
| submit_to_present p99 | 19.655 | 19.655–19.655 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 27.843 | 29.097 | 29.696 | 30.217 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 1.70 | 1.70–1.70 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | True | 300 / 300 | 0 | 300 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 225 / 225 | 0 | 0.0006 |

| Render device counter | Steady per frame, mean over runs | Run min–max | Max in a frame | Run total, mean |
|---|---:|---:|---:|---:|
| bufferPartialWriteAllocations | 0.000 | 0.000–0.000 | 3 | 3.0 |
| bufferPartialWriteAllocatedBytes | 0.000 | 0.000–0.000 | 41943040 | 41943040.0 |
| bufferStagedPatches | 6.000 | 6.000–6.000 | 9 | 1803.0 |
| bufferStagedBytes | 16015776.000 | 16015776.000–16015776.000 | 36140776 | 4824857800.0 |
| bufferFullReplacements | 13.000 | 13.000–13.000 | 14 | 3901.0 |
| bufferMidFrameWaits | 0.000 | 0.000–0.000 | 0 | 0.0 |

| GPU process allocation peak | Mean MiB | Run min–max MiB |
|---|---:|---:|
| sampled at each frame end | 313.7 | 313.7–313.7 |
