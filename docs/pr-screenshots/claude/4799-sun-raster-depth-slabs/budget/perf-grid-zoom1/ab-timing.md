| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| perf-grid-zoom1 | cpu:BakeSunShadowMap | 0.048 (0.036..0.057, 0.009) | per-pixel | 0.051 (0.045..0.057) | 1.058x | +0.003 (0.007) | -0.004 | reported | - |
| perf-grid-zoom1 | cpu:CanvasToFramebuffer | 0.084 (0.069..0.094, 0.010) | per-pixel | 0.090 (0.079..0.096) | 1.067x | +0.006 (0.010) | -0.004 | reported | - |
| perf-grid-zoom1 | cpu:ComputeSunShadow | 0.011 (0.008..0.012, 0.002) | per-pixel | 0.012 (0.011..0.012) | 1.094x | +0.001 (0.002) | -0.001 | reported | - |
| perf-grid-zoom1 | cpu:LightingToTrixel | 0.027 (0.021..0.031, 0.004) | per-pixel | 0.029 (0.026..0.031) | 1.074x | +0.002 (0.003) | -0.001 | reported | - |
| perf-grid-zoom1 | cpu:SingleVoxelToCanvasFirst | 1.969 (1.478..2.401, 0.412) | per-pixel | 2.088 (1.876..2.299) | 1.060x | +0.119 (0.385) | -0.248 | reported | - |
| perf-grid-zoom1 | gpu:bakeSunShadowMap | 0.374 (0.334..0.404, 0.031) | per-pixel | 0.387 (0.343..0.412) | 1.035x | +0.013 (0.048) | -0.033 | reported | - |
| perf-grid-zoom1 | gpu:canvasClear | 0.102 (0.095..0.108, 0.007) | per-pixel | 0.118 (0.113..0.122) | 1.155x | +0.016 (0.010) | +0.006 | reported | - |
| perf-grid-zoom1 | gpu:computeLightVolume | 2.922 (2.636..3.123, 0.237) | per-pixel | 3.013 (2.676..3.134) | 1.031x | +0.091 (0.373) | -0.265 | reported | - |
| perf-grid-zoom1 | gpu:computeSunShadow | 0.122 (0.107..0.133, 0.012) | per-pixel | 0.127 (0.111..0.133) | 1.041x | +0.005 (0.018) | -0.012 | reported | - |
| perf-grid-zoom1 | gpu:computeVoxelAO | 0.055 (0.049..0.059, 0.005) | per-pixel | 0.055 (0.049..0.058) | 1.004x | +0.000 (0.007) | -0.006 | reported | - |
| perf-grid-zoom1 | gpu:fbToScreen | 0.117 (0.105..0.126, 0.010) | per-pixel | 0.121 (0.107..0.126) | 1.031x | +0.004 (0.015) | -0.011 | reported | - |
| perf-grid-zoom1 | gpu:fogToTrixel | 0.031 (0.026..0.034, 0.003) | per-pixel | 0.032 (0.029..0.033) | 1.033x | +0.001 (0.004) | -0.003 | reported | - |
| perf-grid-zoom1 | gpu:lightingToTrixel | 0.056 (0.046..0.063, 0.007) | per-pixel | 0.060 (0.056..0.063) | 1.076x | +0.004 (0.009) | -0.004 | reported | - |
| perf-grid-zoom1 | gpu:textToTrixel | 1.557 (1.419..1.700, 0.127) | per-pixel | 1.654 (1.507..1.744) | 1.062x | +0.096 (0.198) | -0.092 | reported | - |
| perf-grid-zoom1 | gpu:trixelToFb | 1.232 (1.154..1.308, 0.075) | per-pixel | 0.967 (0.888..1.072) | 0.785x | -0.265 (0.087) | -0.348 | reported | - |
| perf-grid-zoom1 | gpu:voxelCardinalElect | 0.446 (0.410..0.471, 0.032) | per-pixel | 0.457 (0.403..0.476) | 1.024x | +0.011 (0.053) | -0.040 | reported | - |
| perf-grid-zoom1 | gpu:voxelCompact | 0.903 (0.786..0.996, 0.088) | per-pixel | 0.733 (0.668..0.800) | 0.812x | -0.170 (0.104) | -0.269 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage1 | 0.546 (0.479..0.594, 0.049) | per-pixel | 0.557 (0.504..0.587) | 1.020x | +0.011 (0.072) | -0.058 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage2 | 0.440 (0.403..0.466, 0.033) | per-pixel | 0.449 (0.394..0.466) | 1.020x | +0.009 (0.055) | -0.043 | reported | - |
| perf-grid-zoom1 | gpu:voxelSunFaces | 1.165 (1.069..1.234, 0.078) | per-pixel | 1.134 (1.008..1.171) | 0.973x | -0.031 (0.128) | -0.153 | reported | - |
| perf-grid-zoom1 | gpu_envelope_ms | 5.327 (4.819..5.706, 0.407) | per-pixel | 5.595 (5.097..5.849) | 1.050x | +0.268 (0.615) | -0.319 | reported | - |
| perf-grid-zoom1 | steady_mean_ms | 23.462 (20.380..25.040, 1.878) | per-pixel | 24.404 (23.620..24.720) | 1.040x | +0.942 (2.016) | -0.981 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | steady_p99_ms | 31.180 (26.920..33.140, 2.485) | per-pixel | 32.840 (31.400..34.110) | 1.053x | +1.660 (3.488) | -1.665 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | cpu:BakeSunShadowMap | 0.048 (0.036..0.057, 0.009) | fills | 0.049 (0.039..0.055) | 1.017x | +0.001 (0.010) | -0.008 | reported | - |
| perf-grid-zoom1 | cpu:CanvasToFramebuffer | 0.084 (0.069..0.094, 0.010) | fills | 0.087 (0.080..0.094) | 1.040x | +0.003 (0.010) | -0.007 | reported | - |
| perf-grid-zoom1 | cpu:ComputeSunShadow | 0.011 (0.008..0.012, 0.002) | fills | 0.012 (0.010..0.013) | 1.113x | +0.001 (0.001) | -0.000 | reported | - |
| perf-grid-zoom1 | cpu:LightingToTrixel | 0.027 (0.021..0.031, 0.004) | fills | 0.029 (0.023..0.032) | 1.051x | +0.001 (0.005) | -0.003 | reported | - |
| perf-grid-zoom1 | cpu:SingleVoxelToCanvasFirst | 1.969 (1.478..2.401, 0.412) | fills | 2.077 (1.565..2.363) | 1.055x | +0.108 (0.417) | -0.290 | reported | - |
| perf-grid-zoom1 | gpu:bakeSunShadowMap | 0.374 (0.334..0.404, 0.031) | fills | 0.391 (0.380..0.400) | 1.045x | +0.017 (0.030) | -0.011 | reported | - |
| perf-grid-zoom1 | gpu:canvasClear | 0.102 (0.095..0.108, 0.007) | fills | 0.063 (0.058..0.068) | 0.614x | -0.039 (0.006) | -0.045 | reported | - |
| perf-grid-zoom1 | gpu:computeLightVolume | 2.922 (2.636..3.123, 0.237) | fills | 3.537 (3.495..3.578) | 1.211x | +0.615 (0.267) | +0.361 | reported | - |
| perf-grid-zoom1 | gpu:computeSunShadow | 0.122 (0.107..0.133, 0.012) | fills | 0.132 (0.128..0.135) | 1.080x | +0.010 (0.010) | -0.000 | reported | - |
| perf-grid-zoom1 | gpu:computeVoxelAO | 0.055 (0.049..0.059, 0.005) | fills | 0.057 (0.054..0.060) | 1.044x | +0.002 (0.004) | -0.002 | reported | - |
| perf-grid-zoom1 | gpu:fbToScreen | 0.117 (0.105..0.126, 0.010) | fills | 0.124 (0.122..0.126) | 1.058x | +0.007 (0.010) | -0.002 | reported | - |
| perf-grid-zoom1 | gpu:fogToTrixel | 0.031 (0.026..0.034, 0.003) | fills | 0.032 (0.030..0.034) | 1.052x | +0.002 (0.003) | -0.001 | reported | - |
| perf-grid-zoom1 | gpu:lightingToTrixel | 0.056 (0.046..0.063, 0.007) | fills | 0.061 (0.055..0.067) | 1.094x | +0.005 (0.007) | -0.001 | reported | - |
| perf-grid-zoom1 | gpu:textToTrixel | 1.557 (1.419..1.700, 0.127) | fills | 1.804 (1.736..1.845) | 1.159x | +0.247 (0.124) | +0.129 | reported | - |
| perf-grid-zoom1 | gpu:trixelToFb | 1.232 (1.154..1.308, 0.075) | fills | 0.809 (0.720..0.885) | 0.656x | -0.424 (0.115) | -0.533 | reported | - |
| perf-grid-zoom1 | gpu:voxelCardinalElect | 0.446 (0.410..0.471, 0.032) | fills | 0.469 (0.467..0.472) | 1.051x | +0.023 (0.031) | -0.007 | reported | - |
| perf-grid-zoom1 | gpu:voxelCompact | 0.903 (0.786..0.996, 0.088) | fills | 0.529 (0.414..0.609) | 0.586x | -0.374 (0.075) | -0.446 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage1 | 0.546 (0.479..0.594, 0.049) | fills | 0.566 (0.529..0.588) | 1.037x | +0.020 (0.047) | -0.025 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage2 | 0.440 (0.403..0.466, 0.033) | fills | 0.464 (0.461..0.468) | 1.055x | +0.024 (0.032) | -0.006 | reported | - |
| perf-grid-zoom1 | gpu:voxelSunFaces | 1.165 (1.069..1.234, 0.078) | fills | 1.171 (1.156..1.182) | 1.005x | +0.006 (0.079) | -0.069 | reported | - |
| perf-grid-zoom1 | gpu_envelope_ms | 5.327 (4.819..5.706, 0.407) | fills | 5.822 (5.668..5.921) | 1.093x | +0.495 (0.387) | +0.126 | reported | - |
| perf-grid-zoom1 | steady_mean_ms | 23.462 (20.380..25.040, 1.878) | fills | 24.488 (21.650..25.560) | 1.044x | +1.026 (2.226) | -1.097 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | steady_p99_ms | 31.180 (26.920..33.140, 2.485) | fills | 33.678 (31.500..36.980) | 1.080x | +2.498 (4.447) | -1.742 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | cpu:BakeSunShadowMap | 0.048 (0.036..0.057, 0.009) | head | 0.050 (0.042..0.055) | 1.025x | +0.001 (0.010) | -0.008 | reported | - |
| perf-grid-zoom1 | cpu:CanvasToFramebuffer | 0.084 (0.069..0.094, 0.010) | head | 0.088 (0.073..0.096) | 1.043x | +0.004 (0.009) | -0.005 | reported | - |
| perf-grid-zoom1 | cpu:ComputeSunShadow | 0.011 (0.008..0.012, 0.002) | head | 0.012 (0.010..0.013) | 1.094x | +0.001 (0.001) | -0.000 | reported | - |
| perf-grid-zoom1 | cpu:LightingToTrixel | 0.027 (0.021..0.031, 0.004) | head | 0.028 (0.025..0.031) | 1.037x | +0.001 (0.005) | -0.003 | reported | - |
| perf-grid-zoom1 | cpu:SingleVoxelToCanvasFirst | 1.969 (1.478..2.401, 0.412) | head | 2.061 (1.662..2.300) | 1.047x | +0.093 (0.422) | -0.310 | reported | - |
| perf-grid-zoom1 | gpu:bakeSunShadowMap | 0.374 (0.334..0.404, 0.031) | head | 0.379 (0.315..0.403) | 1.013x | +0.005 (0.051) | -0.043 | reported | - |
| perf-grid-zoom1 | gpu:canvasClear | 0.102 (0.095..0.108, 0.007) | head | 0.062 (0.055..0.065) | 0.605x | -0.040 (0.009) | -0.049 | reported | - |
| perf-grid-zoom1 | gpu:computeLightVolume | 2.922 (2.636..3.123, 0.237) | head | 3.382 (2.971..3.524) | 1.157x | +0.460 (0.362) | +0.115 | reported | - |
| perf-grid-zoom1 | gpu:computeSunShadow | 0.122 (0.107..0.133, 0.012) | head | 0.126 (0.103..0.135) | 1.029x | +0.004 (0.018) | -0.013 | reported | - |
| perf-grid-zoom1 | gpu:computeVoxelAO | 0.055 (0.049..0.059, 0.005) | head | 0.055 (0.046..0.060) | 1.015x | +0.001 (0.007) | -0.006 | reported | - |
| perf-grid-zoom1 | gpu:fbToScreen | 0.117 (0.105..0.126, 0.010) | head | 0.120 (0.099..0.126) | 1.019x | +0.002 (0.017) | -0.014 | reported | - |
| perf-grid-zoom1 | gpu:fogToTrixel | 0.031 (0.026..0.034, 0.003) | head | 0.031 (0.025..0.034) | 1.020x | +0.001 (0.005) | -0.004 | reported | - |
| perf-grid-zoom1 | gpu:lightingToTrixel | 0.056 (0.046..0.063, 0.007) | head | 0.057 (0.046..0.064) | 1.032x | +0.002 (0.009) | -0.007 | reported | - |
| perf-grid-zoom1 | gpu:textToTrixel | 1.557 (1.419..1.700, 0.127) | head | 1.708 (1.404..1.836) | 1.097x | +0.151 (0.248) | -0.086 | reported | - |
| perf-grid-zoom1 | gpu:trixelToFb | 1.232 (1.154..1.308, 0.075) | head | 0.814 (0.755..0.900) | 0.660x | -0.418 (0.106) | -0.519 | reported | - |
| perf-grid-zoom1 | gpu:voxelCardinalElect | 0.446 (0.410..0.471, 0.032) | head | 0.452 (0.383..0.476) | 1.013x | +0.006 (0.057) | -0.049 | reported | - |
| perf-grid-zoom1 | gpu:voxelCompact | 0.903 (0.786..0.996, 0.088) | head | 0.523 (0.423..0.605) | 0.579x | -0.381 (0.102) | -0.478 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage1 | 0.546 (0.479..0.594, 0.049) | head | 0.546 (0.448..0.587) | 1.000x | -0.000 (0.081) | -0.077 | reported | - |
| perf-grid-zoom1 | gpu:voxelStage2 | 0.440 (0.403..0.466, 0.033) | head | 0.447 (0.379..0.466) | 1.015x | +0.007 (0.058) | -0.049 | reported | - |
| perf-grid-zoom1 | gpu:voxelSunFaces | 1.165 (1.069..1.234, 0.078) | head | 1.132 (0.972..1.179) | 0.971x | -0.033 (0.135) | -0.163 | reported | - |
| perf-grid-zoom1 | gpu_envelope_ms | 5.327 (4.819..5.706, 0.407) | head | 5.614 (4.759..5.926) | 1.054x | +0.287 (0.701) | -0.382 | reported | - |
| perf-grid-zoom1 | steady_mean_ms | 23.462 (20.380..25.040, 1.878) | head | 24.504 (22.860..25.460) | 1.044x | +1.042 (1.139) | -0.044 | ratio <= 1.10x | pass |
| perf-grid-zoom1 | steady_p99_ms | 31.180 (26.920..33.140, 2.485) | head | 32.520 (30.410..33.870) | 1.043x | +1.340 (1.448) | -0.040 | ratio <= 1.10x | pass |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| perf-grid-zoom1 | baseline | 19..20 ms 16%, 21..22 ms 9%, 22..23 ms 15%, 23..24 ms 12% |
| perf-grid-zoom1 | per-pixel | 21..22 ms 11%, 22..23 ms 20%, 23..24 ms 13%, 24..25 ms 11% |
| perf-grid-zoom1 | fills | 19..20 ms 10%, 22..23 ms 16%, 23..24 ms 15%, 26..27 ms 12% |
| perf-grid-zoom1 | head | 22..23 ms 16%, 23..24 ms 15%, 25..26 ms 9%, 26..27 ms 13% |
