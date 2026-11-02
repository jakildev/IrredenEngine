| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.053 (0.042..0.060, 0.007) | per-pixel | 0.056 (0.043..0.071) | 1.061x | +0.003 (0.018) | -0.014 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.173 (0.141..0.195, 0.022) | per-pixel | 0.188 (0.146..0.229) | 1.089x | +0.015 (0.059) | -0.040 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.022 (0.019..0.024, 0.002) | per-pixel | 0.024 (0.018..0.030) | 1.063x | +0.001 (0.007) | -0.005 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.060 (0.050..0.068, 0.007) | per-pixel | 0.063 (0.049..0.078) | 1.040x | +0.002 (0.019) | -0.016 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.383 (0.308..0.446, 0.053) | per-pixel | 0.438 (0.339..0.548) | 1.143x | +0.055 (0.144) | -0.082 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.520 (1.472..1.537, 0.027) | per-pixel | 3.183 (3.139..3.219) | 2.094x | +1.663 (0.055) | +1.611 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.039 (0.035..0.041, 0.002) | per-pixel | 0.035 (0.032..0.039) | 0.907x | -0.004 (0.005) | -0.008 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.889 (1.802..2.000, 0.082) | per-pixel | 3.577 (3.551..3.606) | 1.894x | +1.688 (0.102) | +1.591 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.385 (0.316..0.461, 0.054) | per-pixel | 0.134 (0.124..0.146) | 0.348x | -0.251 (0.056) | -0.305 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.078 (0.065..0.106, 0.017) | per-pixel | 0.101 (0.084..0.113) | 1.286x | +0.022 (0.029) | -0.005 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.106 (0.095..0.114, 0.008) | per-pixel | 0.126 (0.116..0.139) | 1.193x | +0.020 (0.013) | +0.008 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.184 (0.156..0.216, 0.026) | per-pixel | 0.138 (0.128..0.150) | 0.747x | -0.047 (0.033) | -0.078 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.063 (0.054..0.074, 0.009) | per-pixel | 0.065 (0.057..0.073) | 1.032x | +0.002 (0.014) | -0.011 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.595 (1.556..1.628, 0.031) | per-pixel | 1.323 (1.292..1.354) | 0.829x | -0.272 (0.034) | -0.304 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.205 (0.200..0.215, 0.006) | per-pixel | 0.411 (0.385..0.431) | 1.999x | +0.205 (0.029) | +0.177 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.113 (0.090..0.126, 0.014) | per-pixel | 0.123 (0.112..0.132) | 1.083x | +0.009 (0.014) | -0.004 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.107 (0.103..0.123, 0.009) | per-pixel | 0.103 (0.097..0.110) | 0.955x | -0.005 (0.009) | -0.014 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.208 (0.204..0.213, 0.004) | per-pixel | 0.232 (0.220..0.243) | 1.118x | +0.024 (0.008) | +0.016 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.694 (2.544..2.818, 0.102) | per-pixel | 4.328 (4.275..4.402) | 1.606x | +1.634 (0.133) | +1.507 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 11.676 (10.380..13.240, 1.432) | per-pixel | 16.506 (12.830..19.790) | 1.414x | +4.830 (4.224) | +0.802 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 21.112 (19.080..23.380, 1.763) | per-pixel | 24.488 (23.750..24.840) | 1.160x | +3.376 (2.099) | +1.375 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | cpu:BakeSunShadowMap | 0.053 (0.042..0.060, 0.007) | head | 0.066 (0.049..0.094) | 1.246x | +0.013 (0.018) | -0.004 | reported | - |
| canvas-stacked-128 | cpu:CanvasToFramebuffer | 0.173 (0.141..0.195, 0.022) | head | 0.158 (0.117..0.218) | 0.913x | -0.015 (0.037) | -0.050 | reported | - |
| canvas-stacked-128 | cpu:ComputeSunShadow | 0.022 (0.019..0.024, 0.002) | head | 0.021 (0.016..0.030) | 0.928x | -0.002 (0.006) | -0.008 | reported | - |
| canvas-stacked-128 | cpu:LightingToTrixel | 0.060 (0.050..0.068, 0.007) | head | 0.053 (0.039..0.075) | 0.874x | -0.008 (0.013) | -0.020 | reported | - |
| canvas-stacked-128 | cpu:SingleVoxelToCanvasFirst | 0.383 (0.308..0.446, 0.053) | head | 0.367 (0.265..0.521) | 0.957x | -0.016 (0.087) | -0.100 | reported | - |
| canvas-stacked-128 | gpu:bakeSunShadowMap | 1.520 (1.472..1.537, 0.027) | head | 3.206 (3.129..3.291) | 2.109x | +1.686 (0.066) | +1.623 | delta <= 0.029 ms | FAIL |
| canvas-stacked-128 | gpu:canvasClear | 0.039 (0.035..0.041, 0.002) | head | 0.033 (0.030..0.035) | 0.850x | -0.006 (0.003) | -0.009 | reported | - |
| canvas-stacked-128 | gpu:cardinalSurfaceFb | 1.889 (1.802..2.000, 0.082) | head | 3.565 (3.408..3.686) | 1.887x | +1.676 (0.164) | +1.520 | reported | - |
| canvas-stacked-128 | gpu:computeLightVolume | 0.385 (0.316..0.461, 0.054) | head | 0.148 (0.124..0.171) | 0.384x | -0.237 (0.044) | -0.279 | reported | - |
| canvas-stacked-128 | gpu:computeSunShadow | 0.078 (0.065..0.106, 0.017) | head | 0.102 (0.082..0.110) | 1.299x | +0.023 (0.016) | +0.008 | reported | - |
| canvas-stacked-128 | gpu:computeVoxelAO | 0.106 (0.095..0.114, 0.008) | head | 0.263 (0.243..0.273) | 2.492x | +0.158 (0.009) | +0.149 | reported | - |
| canvas-stacked-128 | gpu:fbToScreen | 0.184 (0.156..0.216, 0.026) | head | 0.134 (0.127..0.151) | 0.725x | -0.051 (0.034) | -0.083 | reported | - |
| canvas-stacked-128 | gpu:lightingToTrixel | 0.063 (0.054..0.074, 0.009) | head | 0.063 (0.056..0.067) | 0.997x | -0.000 (0.006) | -0.006 | reported | - |
| canvas-stacked-128 | gpu:trixelToFb | 1.595 (1.556..1.628, 0.031) | head | 1.346 (1.243..1.471) | 0.844x | -0.248 (0.118) | -0.361 | reported | - |
| canvas-stacked-128 | gpu:voxelCardinalElect | 0.205 (0.200..0.215, 0.006) | head | 0.148 (0.140..0.153) | 0.719x | -0.058 (0.007) | -0.064 | reported | - |
| canvas-stacked-128 | gpu:voxelCompact | 0.113 (0.090..0.126, 0.014) | head | 0.109 (0.099..0.128) | 0.963x | -0.004 (0.013) | -0.016 | reported | - |
| canvas-stacked-128 | gpu:voxelStage1 | 0.107 (0.103..0.123, 0.009) | head | 0.096 (0.088..0.099) | 0.890x | -0.012 (0.009) | -0.021 | reported | - |
| canvas-stacked-128 | gpu:voxelStage2 | 0.208 (0.204..0.213, 0.004) | head | 0.166 (0.161..0.169) | 0.800x | -0.042 (0.003) | -0.045 | reported | - |
| canvas-stacked-128 | gpu_envelope_ms | 2.694 (2.544..2.818, 0.102) | head | 4.296 (4.165..4.415) | 1.595x | +1.602 (0.136) | +1.472 | reported | - |
| canvas-stacked-128 | steady_mean_ms | 11.676 (10.380..13.240, 1.432) | head | 15.148 (12.340..18.940) | 1.297x | +3.472 (1.551) | +1.993 | ratio <= 1.10x | FAIL |
| canvas-stacked-128 | steady_p99_ms | 21.112 (19.080..23.380, 1.763) | head | 24.688 (23.370..25.270) | 1.169x | +3.576 (1.575) | +2.074 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-stacked-128 | baseline | 7..8 ms 9%, 8..9 ms 56%, 17..18 ms 17%, 18..19 ms 9% |
| canvas-stacked-128 | per-pixel | 8..9 ms 29%, 18..19 ms 14%, 19..20 ms 22%, 20..21 ms 12% |
| canvas-stacked-128 | head | 8..9 ms 40%, 18..19 ms 10%, 19..20 ms 17%, 20..21 ms 12% |
