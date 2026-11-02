| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.054 (0.036..0.064, 0.011) | per-pixel | 0.056 (0.052..0.060) | 1.049x | +0.003 (0.012) | -0.009 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.173 (0.130..0.202, 0.029) | per-pixel | 0.186 (0.176..0.200) | 1.076x | +0.013 (0.033) | -0.018 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.022 (0.016..0.026, 0.004) | per-pixel | 0.024 (0.022..0.025) | 1.062x | +0.001 (0.005) | -0.003 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.058 (0.041..0.069, 0.010) | per-pixel | 0.063 (0.059..0.067) | 1.079x | +0.005 (0.011) | -0.006 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.357 (0.263..0.420, 0.065) | per-pixel | 0.393 (0.372..0.414) | 1.100x | +0.036 (0.070) | -0.031 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.518 (1.458..1.555, 0.036) | per-pixel | 2.935 (2.914..2.952) | 1.934x | +1.418 (0.049) | +1.371 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.035 (0.030..0.040, 0.004) | per-pixel | 0.033 (0.031..0.037) | 0.954x | -0.002 (0.005) | -0.007 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.925 (1.698..2.053, 0.137) | per-pixel | 3.249 (3.220..3.264) | 1.687x | +1.323 (0.147) | +1.183 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.367 (0.273..0.448, 0.080) | per-pixel | 0.149 (0.138..0.161) | 0.405x | -0.218 (0.085) | -0.299 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.083 (0.053..0.102, 0.018) | per-pixel | 0.067 (0.057..0.078) | 0.800x | -0.017 (0.026) | -0.042 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.102 (0.094..0.121, 0.011) | per-pixel | 0.128 (0.115..0.143) | 1.264x | +0.027 (0.023) | +0.005 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.217 (0.132..0.296, 0.067) | per-pixel | 0.126 (0.115..0.133) | 0.582x | -0.091 (0.072) | -0.159 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.065 (0.054..0.074, 0.008) | per-pixel | 0.060 (0.053..0.067) | 0.926x | -0.005 (0.012) | -0.016 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.617 (1.462..1.697, 0.097) | per-pixel | 1.337 (1.243..1.406) | 0.827x | -0.280 (0.057) | -0.334 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.190 (0.175..0.203, 0.011) | per-pixel | 0.383 (0.359..0.411) | 2.017x | +0.193 (0.024) | +0.170 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.107 (0.093..0.118, 0.011) | per-pixel | 0.119 (0.113..0.124) | 1.120x | +0.013 (0.010) | +0.003 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.098 (0.086..0.109, 0.009) | per-pixel | 0.096 (0.092..0.098) | 0.980x | -0.002 (0.009) | -0.011 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.207 (0.204..0.213, 0.004) | per-pixel | 0.247 (0.226..0.258) | 1.196x | +0.041 (0.014) | +0.027 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.753 (2.432..2.965, 0.209) | per-pixel | 4.023 (3.969..4.051) | 1.461x | +1.270 (0.228) | +1.053 | reported | - |
| canvas-lower | steady_mean_ms | 8.480 (8.390..8.620, 0.094) | per-pixel | 13.482 (9.730..15.050) | 1.590x | +5.002 (2.131) | +2.970 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 15.694 (8.700..17.830, 3.922) | per-pixel | 21.142 (18.690..22.190) | 1.347x | +5.448 (2.548) | +3.019 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.054 (0.036..0.064, 0.011) | head | 0.078 (0.063..0.093) | 1.451x | +0.024 (0.018) | +0.007 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.173 (0.130..0.202, 0.029) | head | 0.184 (0.150..0.220) | 1.062x | +0.011 (0.039) | -0.026 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.022 (0.016..0.026, 0.004) | head | 0.025 (0.021..0.028) | 1.098x | +0.002 (0.005) | -0.003 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.058 (0.041..0.069, 0.010) | head | 0.065 (0.052..0.075) | 1.110x | +0.006 (0.011) | -0.004 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.357 (0.263..0.420, 0.065) | head | 0.391 (0.323..0.452) | 1.095x | +0.034 (0.085) | -0.047 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.518 (1.458..1.555, 0.036) | head | 3.059 (2.936..3.154) | 2.016x | +1.542 (0.060) | +1.485 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.035 (0.030..0.040, 0.004) | head | 0.031 (0.030..0.034) | 0.897x | -0.004 (0.004) | -0.008 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.925 (1.698..2.053, 0.137) | head | 3.362 (3.141..3.507) | 1.746x | +1.436 (0.077) | +1.363 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.367 (0.273..0.448, 0.080) | head | 0.154 (0.141..0.178) | 0.420x | -0.213 (0.077) | -0.286 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.083 (0.053..0.102, 0.018) | head | 0.072 (0.070..0.077) | 0.870x | -0.011 (0.018) | -0.028 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.102 (0.094..0.121, 0.011) | head | 0.250 (0.246..0.258) | 2.463x | +0.149 (0.014) | +0.136 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.217 (0.132..0.296, 0.067) | head | 0.127 (0.123..0.130) | 0.584x | -0.090 (0.066) | -0.153 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.065 (0.054..0.074, 0.008) | head | 0.059 (0.053..0.061) | 0.910x | -0.006 (0.006) | -0.011 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.617 (1.462..1.697, 0.097) | head | 1.387 (1.237..1.451) | 0.858x | -0.230 (0.067) | -0.295 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.190 (0.175..0.203, 0.011) | head | 0.128 (0.122..0.134) | 0.677x | -0.061 (0.012) | -0.073 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.107 (0.093..0.118, 0.011) | head | 0.117 (0.107..0.127) | 1.099x | +0.011 (0.011) | -0.000 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.098 (0.086..0.109, 0.009) | head | 0.093 (0.090..0.096) | 0.955x | -0.004 (0.012) | -0.016 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.207 (0.204..0.213, 0.004) | head | 0.196 (0.192..0.201) | 0.950x | -0.010 (0.005) | -0.015 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.753 (2.432..2.965, 0.209) | head | 4.123 (3.960..4.236) | 1.498x | +1.370 (0.144) | +1.233 | reported | - |
| canvas-lower | steady_mean_ms | 8.480 (8.390..8.620, 0.094) | head | 14.342 (12.900..16.120) | 1.691x | +5.862 (1.348) | +4.577 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 15.694 (8.700..17.830, 3.922) | head | 21.998 (21.550..22.650) | 1.402x | +6.304 (3.707) | +2.769 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 7..8 ms 7%, 8..9 ms 91%, 16..17 ms 0%, 17..18 ms 1% |
| canvas-lower | per-pixel | 8..9 ms 45%, 17..18 ms 19%, 18..19 ms 19%, 19..20 ms 7% |
| canvas-lower | head | 8..9 ms 37%, 17..18 ms 14%, 18..19 ms 21%, 19..20 ms 12% |
