| Measurement | Mean ms | Run min–max ms | One-sided 95% upper bound ms |
|---|---:|---:|---:|
| frame avg | 298.080 | 298.080–298.080 | n/a |
| frame p95 | 332.680 | 332.680–332.680 | n/a |
| frame p99 | 350.320 | 350.320–350.320 | n/a |
| steady frame avg | 294.450 | 294.450–294.450 | n/a |
| steady frame p95 | 323.750 | 323.750–323.750 | n/a |
| steady frame p99 | 343.200 | 343.200–343.200 | n/a |
| frame split INPUT | 0.052 | 0.052–0.052 | n/a |
| frame split UPDATE | 288.651 | 288.651–288.651 | n/a |
| frame split RENDER | 8.281 | 8.281–8.281 | n/a |
| frame split Pacing | 0.000 | 0.000–0.000 | n/a |
| frame split Remainder (includes GPU time) | 1.097 | 1.097–1.097 | n/a |
| input_to_present mean | 99.958 | 99.958–99.958 | n/a |
| input_to_present p99 | 112.731 | 112.731–112.731 | n/a |
| cursor_to_present mean | 64.263 | 64.263–64.263 | n/a |
| cursor_to_present p99 | 70.269 | 70.269–70.269 | n/a |
| frame_start_to_submit mean | 294.502 | 294.502–294.502 | n/a |
| frame_start_to_submit p99 | 343.168 | 343.168–343.168 | n/a |
| submit_to_present mean | 56.233 | 56.233–56.233 | n/a |
| submit_to_present p99 | 61.512 | 61.512–61.512 | n/a |

| Steady frames pooled over runs | Frames | Mean ms | p95 ms | p99 ms | Max ms |
|---|---:|---:|---:|---:|---:|
| first 75 of each run excluded | 225 | 294.454 | 323.748 | 343.201 | 350.323 |

| Fixed updates per rendered frame | Mean | Run min–max | Max in a frame |
|---|---:|---:|---:|
| update ticks | 8.00 | 8.00–8.00 | 8 |

| Run | Frame GPU supported | Valid / attempted | Invalid | Command buffers | Yaw deg (travel) | Overflow max entries / dropped (frames sampled) |
|---|---|---:|---:|---:|---:|---:|
| 1 | False | 0 / 0 | 0 | 0 | 0.000 (0.000) | 0 / 0 (0) |

| Run | Input latency end event | Samples / steady frames | Max frames in flight | Clock mapping error ms |
|---|---|---:|---:|---:|
| run-1 | gpu-complete | 224 / 225 | 1 | 0.1373 |

Render device counters: unsupported by this backend.
