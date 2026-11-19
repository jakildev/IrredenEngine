# opengl Release — interleaved binary / variant A/B

Source `f8c280aa1dc55f24aeb88cc57cb2ce61eaa837ab`; pre binary `a5b14aa2c7eb` (reverts `f8c280aa1`); post binary `a5b14aa2c7eb`.

| Case | Measurement | Contrast | Pairs | After mean (min–max) | Before mean (min–max) | Paired delta | One-sided 95% lower / upper | Delta / before | Upper / before |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| far-128 | GPU entityCanvasToFb ms | post vs pre, production | 5 | 3.218 (2.857–3.367) | 63.865 (63.267–64.189) | -60.647 | -61.002 / -60.291 | -95.0% | -94.4% |
| far-128 | GPU entityCanvasToFb ms | post vs pre, ablation | 5 | 0.352 (0.308–0.376) | 0.285 (0.254–0.297) | +0.067 | +0.054 / +0.080 | 23.5% | 28.2% |
| far-128 | GPU entityCanvasToFb ms | query term, pre | 5 | 63.865 (63.267–64.189) | 0.285 (0.254–0.297) | +63.580 | +63.171 / +63.990 | 22340.3% | 22484.2% |
| far-128 | GPU entityCanvasToFb ms | query term, post | 5 | 3.218 (2.857–3.367) | 0.352 (0.308–0.376) | +2.867 | +2.660 / +3.074 | 815.4% | 874.2% |
| far-128 | steady frame avg ms | post vs pre, production | 5 | 40.446 (36.000–42.080) | 101.614 (100.190–102.200) | -61.168 | -63.708 / -58.628 | -60.2% | -57.7% |
| far-128 | steady frame avg ms | post vs pre, ablation | 5 | 36.316 (31.070–38.690) | 37.064 (31.020–38.740) | -0.748 | -2.306 / +0.810 | -2.0% | 2.2% |
| far-128 | steady frame avg ms | query term, pre | 5 | 101.614 (100.190–102.200) | 37.064 (31.020–38.740) | +64.550 | +61.005 / +68.095 | 174.2% | 183.7% |
| far-128 | steady frame avg ms | query term, post | 5 | 40.446 (36.000–42.080) | 36.316 (31.070–38.690) | +4.130 | +0.368 / +7.892 | 11.4% | 21.7% |

`far-128` production composite cut: upper bound -94.4% of the pre mean; the half-cut gate (≤ −50.0%) is met.

Columns read after = the first arm named in the contrast, before = the second. Rows are sampled invocation spans or frame averages; they are never summed.
