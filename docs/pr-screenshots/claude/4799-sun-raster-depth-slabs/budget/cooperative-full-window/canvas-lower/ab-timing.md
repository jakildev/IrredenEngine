| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.057 (0.054..0.061, 0.003) | per-pixel | 0.049 (0.035..0.060) | 0.870x | -0.007 (0.009) | -0.016 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.182 (0.173..0.192, 0.009) | per-pixel | 0.169 (0.125..0.200) | 0.928x | -0.013 (0.032) | -0.044 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.024 (0.022..0.026, 0.002) | per-pixel | 0.021 (0.015..0.025) | 0.874x | -0.003 (0.004) | -0.007 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.065 (0.061..0.070, 0.004) | per-pixel | 0.055 (0.039..0.067) | 0.852x | -0.010 (0.011) | -0.020 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.357 (0.339..0.379, 0.017) | per-pixel | 0.339 (0.252..0.398) | 0.949x | -0.018 (0.064) | -0.079 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.449 (1.345..1.581, 0.095) | per-pixel | 2.742 (2.644..2.801) | 1.893x | +1.293 (0.108) | +1.190 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.039 (0.033..0.048, 0.006) | per-pixel | 0.038 (0.033..0.044) | 0.974x | -0.001 (0.008) | -0.009 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.984 (1.856..2.157, 0.123) | per-pixel | 3.288 (3.154..3.366) | 1.657x | +1.304 (0.132) | +1.179 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.386 (0.337..0.430, 0.043) | per-pixel | 0.124 (0.117..0.137) | 0.321x | -0.262 (0.042) | -0.303 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.064 (0.054..0.074, 0.008) | per-pixel | 0.080 (0.060..0.097) | 1.245x | +0.016 (0.017) | -0.001 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.113 (0.095..0.150, 0.022) | per-pixel | 0.136 (0.131..0.143) | 1.198x | +0.022 (0.024) | -0.000 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.124 (0.114..0.134, 0.009) | per-pixel | 0.125 (0.113..0.132) | 1.008x | +0.001 (0.012) | -0.010 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.066 (0.059..0.071, 0.005) | per-pixel | 0.074 (0.061..0.083) | 1.114x | +0.008 (0.012) | -0.004 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.732 (1.611..1.906, 0.117) | per-pixel | 1.429 (1.397..1.474) | 0.825x | -0.303 (0.087) | -0.386 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.199 (0.183..0.209, 0.013) | per-pixel | 0.365 (0.341..0.396) | 1.829x | +0.165 (0.014) | +0.153 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.136 (0.131..0.142, 0.005) | per-pixel | 0.125 (0.113..0.136) | 0.921x | -0.011 (0.008) | -0.018 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.137 (0.108..0.182, 0.029) | per-pixel | 0.134 (0.117..0.151) | 0.978x | -0.003 (0.038) | -0.039 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.201 (0.186..0.209, 0.010) | per-pixel | 0.225 (0.207..0.238) | 1.119x | +0.024 (0.005) | +0.019 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.543 (2.377..2.725, 0.154) | per-pixel | 3.828 (3.684..3.928) | 1.506x | +1.286 (0.170) | +1.124 | reported | - |
| canvas-lower | steady_mean_ms | 9.714 (9.230..10.460, 0.473) | per-pixel | 13.730 (11.350..15.680) | 1.413x | +4.016 (1.535) | +2.552 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 19.130 (17.690..19.800, 0.883) | per-pixel | 21.690 (21.380..21.920) | 1.134x | +2.560 (0.849) | +1.750 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.057 (0.054..0.061, 0.003) | fills | 0.075 (0.055..0.090) | 1.320x | +0.018 (0.013) | +0.006 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.182 (0.173..0.192, 0.009) | fills | 0.180 (0.125..0.212) | 0.989x | -0.002 (0.037) | -0.037 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.024 (0.022..0.026, 0.002) | fills | 0.022 (0.016..0.027) | 0.941x | -0.001 (0.004) | -0.006 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.065 (0.061..0.070, 0.004) | fills | 0.061 (0.043..0.073) | 0.938x | -0.004 (0.011) | -0.015 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.357 (0.339..0.379, 0.017) | fills | 0.363 (0.256..0.428) | 1.018x | +0.006 (0.074) | -0.065 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.449 (1.345..1.581, 0.095) | fills | 2.760 (2.666..2.836) | 1.905x | +1.311 (0.078) | +1.236 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.039 (0.033..0.048, 0.006) | fills | 0.036 (0.030..0.047) | 0.918x | -0.003 (0.011) | -0.014 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.984 (1.856..2.157, 0.123) | fills | 3.233 (3.126..3.303) | 1.629x | +1.249 (0.089) | +1.164 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.386 (0.337..0.430, 0.043) | fills | 0.138 (0.126..0.148) | 0.357x | -0.248 (0.048) | -0.294 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.064 (0.054..0.074, 0.008) | fills | 0.074 (0.066..0.087) | 1.143x | +0.009 (0.012) | -0.003 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.113 (0.095..0.150, 0.022) | fills | 0.244 (0.229..0.254) | 2.154x | +0.131 (0.025) | +0.106 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.124 (0.114..0.134, 0.009) | fills | 0.116 (0.113..0.120) | 0.939x | -0.008 (0.006) | -0.014 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.066 (0.059..0.071, 0.005) | fills | 0.062 (0.053..0.078) | 0.937x | -0.004 (0.010) | -0.013 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.732 (1.611..1.906, 0.117) | fills | 1.422 (1.363..1.462) | 0.821x | -0.310 (0.085) | -0.391 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.199 (0.183..0.209, 0.013) | fills | 0.126 (0.117..0.131) | 0.633x | -0.073 (0.009) | -0.081 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.136 (0.131..0.142, 0.005) | fills | 0.116 (0.099..0.133) | 0.853x | -0.020 (0.010) | -0.030 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.137 (0.108..0.182, 0.029) | fills | 0.118 (0.098..0.139) | 0.860x | -0.019 (0.042) | -0.059 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.201 (0.186..0.209, 0.010) | fills | 0.187 (0.175..0.197) | 0.933x | -0.013 (0.003) | -0.017 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.543 (2.377..2.725, 0.154) | fills | 3.776 (3.656..3.860) | 1.485x | +1.234 (0.104) | +1.134 | reported | - |
| canvas-lower | steady_mean_ms | 9.714 (9.230..10.460, 0.473) | fills | 14.446 (11.300..16.350) | 1.487x | +4.732 (2.157) | +2.676 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 19.130 (17.690..19.800, 0.883) | fills | 22.174 (21.660..22.900) | 1.159x | +3.044 (1.268) | +1.835 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.057 (0.054..0.061, 0.003) | head | 0.074 (0.062..0.085) | 1.306x | +0.017 (0.008) | +0.010 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.182 (0.173..0.192, 0.009) | head | 0.176 (0.147..0.207) | 0.968x | -0.006 (0.020) | -0.025 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.024 (0.022..0.026, 0.002) | head | 0.022 (0.019..0.026) | 0.933x | -0.002 (0.002) | -0.004 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.065 (0.061..0.070, 0.004) | head | 0.060 (0.052..0.069) | 0.923x | -0.005 (0.005) | -0.010 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.357 (0.339..0.379, 0.017) | head | 0.357 (0.300..0.420) | 1.001x | +0.000 (0.046) | -0.043 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.449 (1.345..1.581, 0.095) | head | 3.777 (3.640..3.884) | 2.607x | +2.328 (0.123) | +2.210 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.039 (0.033..0.048, 0.006) | head | 0.028 (0.027..0.029) | 0.716x | -0.011 (0.006) | -0.016 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.984 (1.856..2.157, 0.123) | head | 4.271 (4.091..4.366) | 2.153x | +2.287 (0.117) | +2.175 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.386 (0.337..0.430, 0.043) | head | 2.280 (2.113..2.446) | 5.904x | +1.894 (0.123) | +1.776 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.064 (0.054..0.074, 0.008) | head | 0.114 (0.106..0.121) | 1.764x | +0.049 (0.008) | +0.042 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.113 (0.095..0.150, 0.022) | head | 0.196 (0.187..0.210) | 1.735x | +0.083 (0.024) | +0.061 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.124 (0.114..0.134, 0.009) | head | 0.104 (0.100..0.111) | 0.840x | -0.020 (0.010) | -0.030 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.066 (0.059..0.071, 0.005) | head | 0.074 (0.064..0.085) | 1.114x | +0.008 (0.004) | +0.004 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.732 (1.611..1.906, 0.117) | head | 3.876 (3.696..3.964) | 2.237x | +2.143 (0.126) | +2.023 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.199 (0.183..0.209, 0.013) | head | 0.113 (0.099..0.125) | 0.565x | -0.087 (0.003) | -0.090 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.136 (0.131..0.142, 0.005) | head | 0.093 (0.077..0.113) | 0.685x | -0.043 (0.012) | -0.055 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.137 (0.108..0.182, 0.029) | head | 0.103 (0.094..0.117) | 0.748x | -0.035 (0.031) | -0.064 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.201 (0.186..0.209, 0.010) | head | 0.167 (0.145..0.189) | 0.834x | -0.033 (0.011) | -0.044 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.543 (2.377..2.725, 0.154) | head | 4.738 (4.541..4.831) | 1.864x | +2.196 (0.122) | +2.079 | reported | - |
| canvas-lower | steady_mean_ms | 9.714 (9.230..10.460, 0.473) | head | 15.748 (14.400..17.010) | 1.621x | +6.034 (1.071) | +5.013 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 19.130 (17.690..19.800, 0.883) | head | 24.170 (23.370..24.740) | 1.263x | +5.040 (1.058) | +4.031 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 7..8 ms 22%, 8..9 ms 62%, 16..17 ms 4%, 17..18 ms 9% |
| canvas-lower | per-pixel | 8..9 ms 41%, 17..18 ms 15%, 18..19 ms 22%, 19..20 ms 7% |
| canvas-lower | fills | 8..9 ms 34%, 17..18 ms 14%, 18..19 ms 24%, 19..20 ms 11% |
| canvas-lower | head | 8..9 ms 30%, 17..18 ms 8%, 19..20 ms 14%, 20..21 ms 16% |
