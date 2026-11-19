# opengl Release — interleaved binary / variant A/B

Source `45a220089c6b843cab5fb09007ae34356b2df275`; pre binary `a00ebd91355b` (reverts `84d6f7bc93de19e3c1586f27b4330387aa9ac011`); post binary `71387cb90d78`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | post vs pre, production | 5 | 48.051 (47.883–48.334) | 66.045 (65.755–66.546) | -17.994 | -18.392 / -17.596 | -27.2% | -26.6% |
| far-128 | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 0.304 (0.288–0.322) | 0.308 (0.290–0.331) | -0.004 | -0.012 / +0.003 | -1.4% | 1.1% |
| far-128 | GPU entityCanvasToFb ms | query term, pre | 5 | 66.045 (65.755–66.546) | 0.308 (0.290–0.331) | +65.737 | +65.445 / +66.029 | 21343.1% | 21437.9% |
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 48.051 (47.883–48.334) | 0.304 (0.288–0.322) | +47.747 | +47.574 / +47.920 | 15727.0% | 15784.1% |
| far-128 | steady frame avg ms | post vs pre, production | 5 | 78.498 (78.330–78.780) | 96.540 (96.270–97.090) | -18.042 | -18.348 / -17.736 | -18.7% | -18.4% |
| far-128 | steady frame avg ms | post vs pre, ablation | 5 | 31.000 (30.840–31.260) | 31.032 (30.880–31.180) | -0.032 | -0.207 / +0.143 | -0.1% | 0.5% |
| far-128 | steady frame avg ms | query term, pre | 5 | 96.540 (96.270–97.090) | 31.032 (30.880–31.180) | +65.508 | +65.224 / +65.792 | 211.1% | 212.0% |
| far-128 | steady frame avg ms | query term, post | 5 | 78.498 (78.330–78.780) | 31.000 (30.840–31.260) | +47.498 | +47.317 / +47.679 | 153.2% | 153.8% |

`far-128` production composite cut: upper bound -26.6% of the pre mean; the half-cut gate (≤ −50.0%) is not met.

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.
