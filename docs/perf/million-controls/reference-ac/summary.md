Power source: AC Power. Head: 058b495b3. Shaders `3dd13f23a9c6f483`, runtime scripts `85a4c4c0e04ff77c`. Debug binary `e3ce18931556e5e6`. Release binary `bd3e6f341e363fb3`.

| Case | Frame mean ms (round range) | Per-round means ms | p95 ms | p99 ms | GPU frame envelope ms | Updates / frame | Overflow drop warnings |
|---|---:|---|---:|---:|---:|---:|---:|
| debug-profiling-on-yaw0 | 34.73 (34.00–35.19) | 35.01 / 35.19 / 34.00 | 36.55–39.50 | 39.13–59.83 | 22.20 (21.88–22.44) | 2.1 (2.0–2.1) | 0 |
| debug-profiling-on-yaw45 | 47.57 (46.39–49.80) | 49.80 / 46.53 / 46.39 | 48.37–51.52 | 65.42–69.42 | 32.35 (31.44–33.89) | 2.9 (2.8–3.0) | 0 |
| debug-profiling-off-yaw0 | 34.97 (34.77–35.10) | 35.05 / 34.77 / 35.10 | 36.81–37.61 | 42.88–47.65 | 22.70 (22.57–22.79) | 2.1 (2.1–2.1) | 0 |
| debug-profiling-off-yaw45 | 45.26 (45.01–45.63) | 45.63 / 45.01 / 45.14 | 46.61–49.48 | 60.60–70.38 | 30.83 (30.70–31.02) | 2.7 (2.7–2.7) | 0 |
| release-profiling-on-yaw0 | 34.61 (33.70–35.51) | 34.62 / 33.70 / 35.51 | 34.52–38.33 | 39.39–126.18 | 22.31 (21.89–22.56) | 2.1 (2.0–2.1) | unverified (no engine log) |
| release-profiling-on-yaw45 | 45.47 (43.74–46.59) | 46.59 / 46.08 / 43.74 | 44.96–48.27 | 47.45–112.40 | 30.78 (29.43–31.47) | 2.7 (2.6–2.8) | unverified (no engine log) |
| release-profiling-off-yaw0 | 34.30 (33.79–34.58) | 34.53 / 34.58 / 33.79 | 34.75–36.94 | 40.10–41.10 | 22.41 (21.76–22.74) | 2.1 (2.0–2.1) | unverified (no engine log) |
| release-profiling-off-yaw45 | 44.60 (43.53–45.52) | 45.52 / 44.74 / 43.53 | 45.41–47.26 | 52.45–120.18 | 30.41 (29.48–30.92) | 2.7 (2.6–2.7) | unverified (no engine log) |
