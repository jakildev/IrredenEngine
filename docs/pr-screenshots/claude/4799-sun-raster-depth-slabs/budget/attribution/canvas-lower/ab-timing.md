| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.050 (0.032..0.057, 0.010) | cooperative | 0.068 (0.056..0.081) | 1.345x | +0.017 (0.019) | -0.001 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.148 (0.097..0.166, 0.029) | cooperative | 0.150 (0.128..0.181) | 1.012x | +0.002 (0.047) | -0.043 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.020 (0.013..0.023, 0.004) | cooperative | 0.020 (0.016..0.023) | 0.961x | -0.001 (0.006) | -0.007 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.057 (0.036..0.064, 0.012) | cooperative | 0.054 (0.045..0.065) | 0.954x | -0.003 (0.018) | -0.020 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.278 (0.188..0.312, 0.051) | cooperative | 0.308 (0.263..0.370) | 1.109x | +0.030 (0.089) | -0.055 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.441 (1.435..1.454, 0.008) | cooperative | 4.678 (4.467..4.871) | 3.246x | +3.237 (0.158) | +3.086 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.030, 0.000) | cooperative | 0.028 (0.027..0.029) | 0.952x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.762 (1.743..1.786, 0.016) | cooperative | 4.976 (4.742..5.177) | 2.824x | +3.214 (0.177) | +3.046 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.377 (0.341..0.452, 0.044) | cooperative | 2.323 (1.814..2.754) | 6.157x | +1.945 (0.381) | +1.582 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.042 (0.041..0.042, 0.001) | cooperative | 0.045 (0.044..0.047) | 1.082x | +0.003 (0.001) | +0.003 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.074 (0.073..0.076, 0.001) | cooperative | 0.280 (0.261..0.291) | 3.779x | +0.206 (0.012) | +0.195 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.122 (0.122..0.123, 0.000) | cooperative | 0.117 (0.112..0.122) | 0.957x | -0.005 (0.003) | -0.008 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.049 (0.049..0.050, 0.000) | cooperative | 0.047 (0.046..0.049) | 0.959x | -0.002 (0.001) | -0.003 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.536 (1.514..1.560, 0.018) | cooperative | 4.489 (4.264..4.692) | 2.922x | +2.953 (0.174) | +2.787 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.213 (0.211..0.214, 0.001) | cooperative | 0.113 (0.109..0.118) | 0.533x | -0.099 (0.003) | -0.102 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.057 (0.057..0.058, 0.001) | cooperative | 0.056 (0.055..0.058) | 0.979x | -0.001 (0.001) | -0.002 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.080 (0.080..0.081, 0.001) | cooperative | 0.077 (0.074..0.080) | 0.960x | -0.003 (0.002) | -0.005 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.212 (0.212..0.213, 0.000) | cooperative | 0.162 (0.157..0.169) | 0.764x | -0.050 (0.005) | -0.055 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.275 (2.268..2.291, 0.009) | cooperative | 5.485 (5.241..5.705) | 2.411x | +3.211 (0.181) | +3.038 | reported | - |
| canvas-lower | steady_mean_ms | 8.330 (8.330..8.330, 0.000) | cooperative | 17.072 (14.220..18.430) | 2.049x | +8.742 (1.704) | +7.117 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 8.812 (8.790..8.830, 0.016) | cooperative | 22.756 (22.510..22.980) | 2.582x | +13.944 (0.204) | +13.750 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.050 (0.032..0.057, 0.010) | head | 0.101 (0.065..0.215) | 2.008x | +0.051 (0.074) | -0.020 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.148 (0.097..0.166, 0.029) | head | 0.151 (0.140..0.159) | 1.018x | +0.003 (0.030) | -0.026 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.020 (0.013..0.023, 0.004) | head | 0.021 (0.019..0.022) | 1.020x | +0.000 (0.005) | -0.004 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.057 (0.036..0.064, 0.012) | head | 0.057 (0.052..0.061) | 1.014x | +0.001 (0.012) | -0.011 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.278 (0.188..0.312, 0.051) | head | 0.286 (0.263..0.302) | 1.030x | +0.008 (0.053) | -0.042 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.441 (1.435..1.454, 0.008) | head | 2.098 (2.081..2.110) | 1.456x | +0.657 (0.016) | +0.642 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.030, 0.000) | head | 0.030 (0.029..0.032) | 1.021x | +0.001 (0.002) | -0.001 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.762 (1.743..1.786, 0.016) | head | 2.389 (2.371..2.401) | 1.356x | +0.627 (0.024) | +0.604 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.377 (0.341..0.452, 0.044) | head | 0.116 (0.112..0.122) | 0.308x | -0.261 (0.042) | -0.301 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.042 (0.041..0.042, 0.001) | head | 0.045 (0.045..0.047) | 1.091x | +0.004 (0.001) | +0.003 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.074 (0.073..0.076, 0.001) | head | 0.292 (0.283..0.300) | 3.941x | +0.218 (0.005) | +0.213 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.122 (0.122..0.123, 0.000) | head | 0.123 (0.121..0.124) | 1.003x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.049 (0.049..0.050, 0.000) | head | 0.051 (0.050..0.053) | 1.037x | +0.002 (0.001) | +0.001 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.536 (1.514..1.560, 0.018) | head | 1.168 (1.159..1.177) | 0.760x | -0.369 (0.023) | -0.390 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.213 (0.211..0.214, 0.001) | head | 0.117 (0.116..0.118) | 0.549x | -0.096 (0.002) | -0.097 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.057 (0.057..0.058, 0.001) | head | 0.058 (0.057..0.059) | 1.010x | +0.001 (0.001) | -0.000 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.080 (0.080..0.081, 0.001) | head | 0.081 (0.080..0.083) | 1.002x | +0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.212 (0.212..0.213, 0.000) | head | 0.164 (0.161..0.166) | 0.774x | -0.048 (0.002) | -0.050 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.275 (2.268..2.291, 0.009) | head | 2.940 (2.909..2.958) | 1.293x | +0.666 (0.023) | +0.644 | reported | - |
| canvas-lower | steady_mean_ms | 8.330 (8.330..8.330, 0.000) | head | 8.704 (8.590..8.910) | 1.045x | +0.374 (0.139) | +0.242 | ratio <= 1.10x | pass |
| canvas-lower | steady_p99_ms | 8.812 (8.790..8.830, 0.016) | head | 17.306 (17.280..17.330) | 1.964x | +8.494 (0.019) | +8.475 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 5..6 ms 0%, 7..8 ms 31%, 8..9 ms 69%, 10..11 ms 0% |
| canvas-lower | cooperative | 8..9 ms 22%, 18..19 ms 11%, 19..20 ms 38%, 20..21 ms 11% |
| canvas-lower | head | 7..8 ms 30%, 8..9 ms 66%, 16..17 ms 2%, 17..18 ms 3% |
