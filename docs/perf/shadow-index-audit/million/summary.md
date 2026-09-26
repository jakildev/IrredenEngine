Power source: AC Power (battery 84% to 84%). Host load as each run ended 2.6 to 4.8 on 14 CPUs. Head: 6521c54dd. Shaders `e1f7c0c89ccd2dc3`, runtime scripts `85a4c4c0e04ff77c`. Debug binary `86701b311f87eeae`.

| Case | Frame mean ms (round range) | Per-round means ms | Steady mean / p95 / p99 ms (frames pooled) | All-frame p99 ms | GPU frame envelope ms | Updates / frame | Yaw deg | Overflow max entries / dropped |
|---|---:|---|---:|---:|---:|---:|---:|---:|
| debug-profiling-on-yaw0 | 33.15 (33.09–33.20) | 33.09 / 33.20 | 32.80 / 35.52 / 38.29 (450) | 39.90–40.80 | 21.60 (21.52–21.68) | 2.0 (2.0–2.0) | 0.000 | 0 / 0 |
| debug-profiling-on-yaw45 | 60.98 (60.98–60.99) | 60.98 / 60.99 | 60.52 / 67.71 / 70.48 (450) | 71.89–73.11 | 44.56 (44.52–44.60) | 3.6 (3.6–3.6) | 45.000 | 2208000 / 0 |
| debug-profiling-on-yawsweep | 42.58 (42.55–42.62) | 42.55 / 42.62 | 42.22 / 46.72 / 48.84 (450) | 55.88–56.42 | 28.99 (28.95–29.03) | 2.5 (2.5–2.5) | 0.000 +358.8 | 971724 / 0 |
| debug-profiling-off-yaw0 | 34.17 (33.77–34.58) | 33.77 / 34.58 | 33.90 / 36.73 / 40.16 (450) | 40.68–41.96 | 22.20 (22.18–22.22) | 2.0 (2.0–2.1) | 0.000 | 0 / 0 |
| debug-profiling-off-yaw45 | 60.94 (60.36–61.52) | 61.52 / 60.36 | 60.81 / 67.73 / 70.90 (450) | 71.87–73.55 | 44.54 (44.10–44.98) | 3.7 (3.6–3.7) | 45.000 | 2208000 / 0 |
| debug-profiling-off-yawsweep | 42.45 (42.25–42.66) | 42.66 / 42.25 | 42.18 / 46.67 / 49.41 (450) | 59.20–60.88 | 28.90 (28.84–28.96) | 2.5 (2.5–2.6) | 0.000 +358.8 | 971724 / 0 |
