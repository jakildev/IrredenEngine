# opengl Debug — interleaved binary / variant A/B

Source `f8c280aa1dc55f24aeb88cc57cb2ce61eaa837ab`; pre binary `320005d17eed` (reverts `f8c280aa1`); post binary `320005d17eed`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | post vs pre, production | 5 | 3.339 (3.268–3.431) | 63.213 (62.348–63.751) | -59.874 | -60.395 / -59.353 | -94.7% | -93.9% |
| far-128 | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 0.698 (0.641–0.761) | 0.620 (0.557–0.658) | +0.078 | +0.000 / +0.155 | 12.6% | 25.1% |
| far-128 | GPU entityCanvasToFb ms | query term, pre | 5 | 63.213 (62.348–63.751) | 0.620 (0.557–0.658) | +62.593 | +62.036 / +63.150 | 10098.9% | 10188.7% |
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 3.339 (3.268–3.431) | 0.698 (0.641–0.761) | +2.641 | +2.552 / +2.731 | 378.6% | 391.4% |
| far-128 | steady frame avg ms | post vs pre, production | 5 | 41.328 (41.070–41.780) | 100.724 (99.220–101.670) | -59.396 | -60.485 / -58.307 | -59.0% | -57.9% |
| far-128 | steady frame avg ms | post vs pre, ablation | 5 | 39.010 (37.970–40.310) | 39.578 (38.560–41.560) | -0.568 | -1.981 / +0.845 | -1.4% | 2.1% |
| far-128 | steady frame avg ms | query term, pre | 5 | 100.724 (99.220–101.670) | 39.578 (38.560–41.560) | +61.146 | +59.233 / +63.059 | 154.5% | 159.3% |
| far-128 | steady frame avg ms | query term, post | 5 | 41.328 (41.070–41.780) | 39.010 (37.970–40.310) | +2.318 | +1.381 / +3.255 | 5.9% | 8.3% |

`far-128` production composite cut: upper bound -93.9% of the pre mean; the half-cut gate (≤ −50.0%) is met.

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.
