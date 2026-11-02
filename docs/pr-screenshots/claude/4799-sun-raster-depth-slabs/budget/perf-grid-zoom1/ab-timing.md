| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| perf-grid-zoom1 | cpu:BakeSunShadowMap | 0.051 (0.041..0.057, 0.007) | per-pixel | 0.054 (0.044..0.059) | 1.059x | +0.003 (0.009) | -0.006 | reported | - |
| perf-grid-zoom1 | cpu:CanvasToFramebuffer | 0.091 (0.075..0.101, 0.010) | per-pixel | 0.095 (0.082..0.102) | 1.053x | +0.005 (0.014) | -0.008 | reported | - |
| perf-grid-zoom1 | cpu:ComputeSunShadow | 0.011 (0.009..0.012, 0.001) | per-pixel | 0.012 (0.011..0.013) | 1.107x | +0.001 (0.001) | -0.000 | reported | - |
| perf-grid-zoom1 | cpu:LightingToTrixel | 0.030 (0.025..0.033, 0.004) | per-pixel | 0.032 (0.027..0.035) | 1.081x | +0.002 (0.005) | -0.002 | reported | - |
| perf-grid-zoom1 | cpu:SingleVoxelToCanvasFirst | 2.133 (1.776..2.406, 0.275) | per-pixel | 2.259 (1.863..2.468) | 1.059x | +0.126 (0.395) | -0.251 | reported | - |
| perf-grid-zoom1 | gpu:bakeSunShadowMap | 0.401 (0.386..0.416, 0.012) | per-pixel | 0.408 (0.348..0.440) | 1.018x | +0.007 (0.038) | -0.029 | reported | - |
| perf-grid-zoom1 | gpu:canvasClear | 0.116 (0.098..0.153, 0.021) | per-pixel | 0.133 (0.116..0.144) | 1.139x | +0.016 (0.026) | -0.008 | reported | - |
| perf-grid-zoom1 | gpu:computeLightVolume | 3.105 (3.001..3.237, 0.095) | per-pixel | 3.068 (2.958..3.200) | 0.988x | -0.037 (0.174) | -0.203 | reported | - |
| perf-grid-zoom1 | gpu:computeSunShadow | 0.124 (0.113..0.142, 0.011) | per-pixel | 0.122 (0.110..0.129) | 0.982x | -0.002 (0.017) | -0.019 | reported | - |
| perf-grid-zoom1 | gpu:computeVoxelAO | 0.061 (0.051..0.078, 0.010) | per-pixel | 0.061 (0.053..0.078) | 1.000x | +0.000 (0.018) | -0.017 | reported | - |
| perf-grid-zoom1 | gpu:fbToScreen | 0.120 (0.109..0.128, 0.009) | per-pixel | 0.125 (0.110..0.143) | 1.042x | +0.005 (0.013) | -0.008 | reported | - |
| perf-grid-zoom1 | gpu:fogToTrixel | 0.034 (0.028..0.040, 0.005) | per-pixel | 0.029 (0.028..0.031) | 0.854x | -0.005 (0.005) | -0.010 | reported | - |
| perf-grid-zoom1 | gpu:lightingToTrixel | 0.055 (0.051..0.065, 0.006) | per-pixel | 0.056 (0.054..0.061) | 1.022x | +0.001 (0.007) | -0.005 | reported | - |
| perf-grid-zoom1 | gpu:textToTrixel | 1.284 (1.030..1.386, 0.145) | per-pixel | 1.367 (1.269..1.508) | 1.065x | +0.083 (0.169) | -0.078 | reported | - |
| perf-grid-zoom1 | gpu:trixelToFb | 1.313 (1.218..1.461, 0.098) | per-pixel | 1.201 (1.135..1.272) | 0.915x | -0.112 (0.113) | -0.219 | reported | - |
| perf-grid-zoom1 | gpu:voxelCardinalElect | 0.503 (0.469..0.558, 0.035) | per-pixel | 0.483 (0.445..0.503) | 0.960x | -0.020 (0.024) | -0.043 | reported | - |
| perf-grid-zoom1 | gpu:voxelCompact | 0.803 (0.694..0.869, 0.072) | per-pixel | 0.478 (0.450..0.512) | 0.595x | -0.325 (0.088) | -0.409 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage1 | 0.613 (0.586..0.672, 0.037) | per-pixel | 0.598 (0.576..0.640) | 0.975x | -0.015 (0.050) | -0.063 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage2 | 0.469 (0.445..0.494, 0.022) | per-pixel | 0.440 (0.431..0.446) | 0.940x | -0.028 (0.025) | -0.052 | reported | - |
| perf-grid-zoom1 | gpu:voxelSunFaces | 1.124 (1.087..1.177, 0.038) | per-pixel | 1.063 (1.020..1.094) | 0.945x | -0.062 (0.060) | -0.119 | reported | - |
| perf-grid-zoom1 | gpu_envelope_ms | 5.552 (5.364..5.824, 0.172) | per-pixel | 5.487 (5.391..5.592) | 0.988x | -0.065 (0.257) | -0.310 | reported | - |
| perf-grid-zoom1 | steady_mean_ms | 25.510 (24.160..26.770, 1.064) | per-pixel | 25.612 (22.710..27.090) | 1.004x | +0.102 (1.869) | -1.680 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | steady_p99_ms | 34.832 (33.040..37.210, 1.529) | per-pixel | 34.860 (28.300..37.370) | 1.001x | +0.028 (4.093) | -3.874 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | cpu:BakeSunShadowMap | 0.051 (0.041..0.057, 0.007) | head | 0.051 (0.044..0.058) | 1.012x | +0.001 (0.004) | -0.003 | reported | - |
| perf-grid-zoom1 | cpu:CanvasToFramebuffer | 0.091 (0.075..0.101, 0.010) | head | 0.093 (0.080..0.100) | 1.022x | +0.002 (0.004) | -0.002 | reported | - |
| perf-grid-zoom1 | cpu:ComputeSunShadow | 0.011 (0.009..0.012, 0.001) | head | 0.012 (0.011..0.013) | 1.071x | +0.001 (0.001) | +0.000 | reported | - |
| perf-grid-zoom1 | cpu:LightingToTrixel | 0.030 (0.025..0.033, 0.004) | head | 0.031 (0.026..0.034) | 1.034x | +0.001 (0.002) | -0.001 | reported | - |
| perf-grid-zoom1 | cpu:SingleVoxelToCanvasFirst | 2.133 (1.776..2.406, 0.275) | head | 2.177 (1.891..2.443) | 1.021x | +0.044 (0.177) | -0.124 | reported | - |
| perf-grid-zoom1 | gpu:bakeSunShadowMap | 0.401 (0.386..0.416, 0.012) | head | 0.434 (0.406..0.471) | 1.083x | +0.033 (0.019) | +0.015 | reported | - |
| perf-grid-zoom1 | gpu:canvasClear | 0.116 (0.098..0.153, 0.021) | head | 0.073 (0.065..0.083) | 0.625x | -0.044 (0.022) | -0.065 | reported | - |
| perf-grid-zoom1 | gpu:computeLightVolume | 3.105 (3.001..3.237, 0.095) | head | 3.394 (3.203..3.704) | 1.093x | +0.289 (0.212) | +0.087 | reported | - |
| perf-grid-zoom1 | gpu:computeSunShadow | 0.124 (0.113..0.142, 0.011) | head | 0.124 (0.117..0.135) | 0.998x | -0.000 (0.016) | -0.015 | reported | - |
| perf-grid-zoom1 | gpu:computeVoxelAO | 0.061 (0.051..0.078, 0.010) | head | 0.058 (0.054..0.064) | 0.951x | -0.003 (0.007) | -0.010 | reported | - |
| perf-grid-zoom1 | gpu:fbToScreen | 0.120 (0.109..0.128, 0.009) | head | 0.120 (0.111..0.130) | 1.003x | +0.000 (0.017) | -0.016 | reported | - |
| perf-grid-zoom1 | gpu:fogToTrixel | 0.034 (0.028..0.040, 0.005) | head | 0.030 (0.028..0.034) | 0.889x | -0.004 (0.006) | -0.010 | reported | - |
| perf-grid-zoom1 | gpu:lightingToTrixel | 0.055 (0.051..0.065, 0.006) | head | 0.058 (0.053..0.066) | 1.058x | +0.003 (0.005) | -0.002 | reported | - |
| perf-grid-zoom1 | gpu:textToTrixel | 1.284 (1.030..1.386, 0.145) | head | 1.580 (1.508..1.614) | 1.231x | +0.296 (0.162) | +0.142 | reported | - |
| perf-grid-zoom1 | gpu:trixelToFb | 1.313 (1.218..1.461, 0.098) | head | 1.072 (0.936..1.268) | 0.816x | -0.241 (0.144) | -0.379 | reported | - |
| perf-grid-zoom1 | gpu:voxelCardinalElect | 0.503 (0.469..0.558, 0.035) | head | 0.500 (0.467..0.528) | 0.994x | -0.003 (0.048) | -0.049 | reported | - |
| perf-grid-zoom1 | gpu:voxelCompact | 0.803 (0.694..0.869, 0.072) | head | 0.549 (0.520..0.594) | 0.683x | -0.254 (0.057) | -0.309 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage1 | 0.613 (0.586..0.672, 0.037) | head | 0.609 (0.558..0.649) | 0.993x | -0.004 (0.034) | -0.037 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage2 | 0.469 (0.445..0.494, 0.022) | head | 0.456 (0.422..0.491) | 0.973x | -0.013 (0.034) | -0.045 | reported | - |
| perf-grid-zoom1 | gpu:voxelSunFaces | 1.124 (1.087..1.177, 0.038) | head | 1.097 (1.064..1.149) | 0.976x | -0.027 (0.053) | -0.077 | reported | - |
| perf-grid-zoom1 | gpu_envelope_ms | 5.552 (5.364..5.824, 0.172) | head | 5.752 (5.529..5.983) | 1.036x | +0.200 (0.290) | -0.077 | reported | - |
| perf-grid-zoom1 | steady_mean_ms | 25.510 (24.160..26.770, 1.064) | head | 25.872 (24.580..27.240) | 1.014x | +0.362 (1.277) | -0.855 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | steady_p99_ms | 34.832 (33.040..37.210, 1.529) | head | 35.320 (33.560..36.930) | 1.014x | +0.488 (2.888) | -2.266 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| perf-grid-zoom1 | baseline | 23..24 ms 12%, 24..25 ms 12%, 25..26 ms 9%, 26..27 ms 12% |
| perf-grid-zoom1 | per-pixel | 23..24 ms 13%, 25..26 ms 11%, 26..27 ms 11%, 27..28 ms 10% |
| perf-grid-zoom1 | head | 24..25 ms 16%, 25..26 ms 11%, 26..27 ms 9%, 27..28 ms 11% |
