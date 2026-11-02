| cell | metric | base mean (min..max, sd) | arm | arm mean (min..max) | arm / base | paired delta mean (sd) | lower 95% | limit | verdict |
|---|---|---|---|---|---|---|---|---|---|
| canvas-lower | cpu:BakeSunShadowMap | 0.054 (0.044..0.059, 0.006) | per-pixel | 0.050 (0.041..0.057) | 0.923x | -0.004 (0.012) | -0.016 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.168 (0.153..0.184, 0.011) | per-pixel | 0.162 (0.128..0.189) | 0.964x | -0.006 (0.034) | -0.038 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.022 (0.018..0.025, 0.003) | per-pixel | 0.021 (0.017..0.024) | 0.920x | -0.002 (0.005) | -0.007 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.062 (0.049..0.067, 0.007) | per-pixel | 0.056 (0.047..0.065) | 0.909x | -0.006 (0.014) | -0.019 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.314 (0.288..0.345, 0.022) | per-pixel | 0.324 (0.265..0.378) | 1.033x | +0.010 (0.066) | -0.052 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.451 (1.434..1.465, 0.013) | per-pixel | 2.714 (2.594..2.900) | 1.871x | +1.263 (0.117) | +1.152 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.029, 0.000) | per-pixel | 0.029 (0.029..0.029) | 1.000x | +0.000 (0.000) | +0.000 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.860 (1.762..1.933, 0.086) | per-pixel | 3.118 (2.893..3.422) | 1.676x | +1.258 (0.140) | +1.124 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.385 (0.350..0.429, 0.032) | per-pixel | 0.114 (0.106..0.126) | 0.295x | -0.271 (0.032) | -0.302 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.047 (0.041..0.053, 0.006) | per-pixel | 0.046 (0.044..0.050) | 0.983x | -0.001 (0.004) | -0.005 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.081 (0.072..0.088, 0.007) | per-pixel | 0.141 (0.098..0.167) | 1.747x | +0.060 (0.034) | +0.027 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.123 (0.122..0.124, 0.001) | per-pixel | 0.123 (0.121..0.124) | 0.998x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.052 (0.048..0.056, 0.004) | per-pixel | 0.053 (0.049..0.058) | 1.008x | +0.000 (0.002) | -0.001 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.624 (1.541..1.686, 0.072) | per-pixel | 1.182 (0.997..1.421) | 0.727x | -0.443 (0.110) | -0.547 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.215 (0.211..0.221, 0.004) | per-pixel | 0.298 (0.218..0.428) | 1.382x | +0.082 (0.083) | +0.003 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.088 (0.057..0.111, 0.028) | per-pixel | 0.084 (0.057..0.125) | 0.952x | -0.004 (0.014) | -0.017 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.085 (0.080..0.089, 0.004) | per-pixel | 0.085 (0.080..0.091) | 0.993x | -0.001 (0.002) | -0.002 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.215 (0.213..0.219, 0.003) | per-pixel | 0.166 (0.124..0.229) | 0.769x | -0.050 (0.041) | -0.089 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.374 (2.265..2.453, 0.088) | per-pixel | 3.635 (3.430..3.931) | 1.531x | +1.261 (0.128) | +1.139 | reported | - |
| canvas-lower | steady_mean_ms | 8.850 (8.330..9.490, 0.527) | per-pixel | 13.390 (10.900..15.380) | 1.513x | +4.540 (1.566) | +3.047 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 14.140 (8.800..18.020, 4.868) | per-pixel | 20.306 (18.130..21.370) | 1.436x | +6.166 (3.880) | +2.466 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.054 (0.044..0.059, 0.006) | fills | 0.070 (0.065..0.075) | 1.288x | +0.016 (0.008) | +0.008 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.168 (0.153..0.184, 0.011) | fills | 0.172 (0.153..0.199) | 1.026x | +0.004 (0.026) | -0.020 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.022 (0.018..0.025, 0.003) | fills | 0.021 (0.019..0.022) | 0.929x | -0.002 (0.004) | -0.005 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.062 (0.049..0.067, 0.007) | fills | 0.057 (0.054..0.061) | 0.916x | -0.005 (0.009) | -0.014 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.314 (0.288..0.345, 0.022) | fills | 0.336 (0.310..0.382) | 1.070x | +0.022 (0.046) | -0.022 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.451 (1.434..1.465, 0.013) | fills | 2.942 (2.855..3.076) | 2.028x | +1.491 (0.086) | +1.409 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.029, 0.000) | fills | 0.029 (0.029..0.030) | 1.007x | +0.000 (0.000) | -0.000 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.860 (1.762..1.933, 0.086) | fills | 3.292 (3.137..3.576) | 1.769x | +1.431 (0.146) | +1.292 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.385 (0.350..0.429, 0.032) | fills | 0.142 (0.130..0.155) | 0.370x | -0.242 (0.026) | -0.267 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.047 (0.041..0.053, 0.006) | fills | 0.046 (0.043..0.051) | 0.983x | -0.001 (0.006) | -0.006 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.081 (0.072..0.088, 0.007) | fills | 0.287 (0.258..0.302) | 3.563x | +0.207 (0.024) | +0.184 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.123 (0.122..0.124, 0.001) | fills | 0.122 (0.118..0.125) | 0.992x | -0.001 (0.002) | -0.003 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.052 (0.048..0.056, 0.004) | fills | 0.051 (0.048..0.056) | 0.981x | -0.001 (0.003) | -0.004 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.624 (1.541..1.686, 0.072) | fills | 1.300 (1.191..1.515) | 0.800x | -0.325 (0.108) | -0.427 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.215 (0.211..0.221, 0.004) | fills | 0.124 (0.114..0.146) | 0.574x | -0.092 (0.010) | -0.102 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.088 (0.057..0.111, 0.028) | fills | 0.079 (0.057..0.133) | 0.895x | -0.009 (0.027) | -0.035 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.085 (0.080..0.089, 0.004) | fills | 0.084 (0.080..0.091) | 0.991x | -0.001 (0.004) | -0.004 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.215 (0.213..0.219, 0.003) | fills | 0.179 (0.166..0.210) | 0.830x | -0.037 (0.017) | -0.053 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.374 (2.265..2.453, 0.088) | fills | 3.838 (3.681..4.113) | 1.617x | +1.464 (0.144) | +1.327 | reported | - |
| canvas-lower | steady_mean_ms | 8.850 (8.330..9.490, 0.527) | fills | 13.296 (12.190..14.520) | 1.502x | +4.446 (1.361) | +3.148 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 14.140 (8.800..18.020, 4.868) | fills | 20.760 (19.990..21.480) | 1.468x | +6.620 (4.832) | +2.013 | ratio <= 1.10x | FAIL |
| canvas-lower | cpu:BakeSunShadowMap | 0.054 (0.044..0.059, 0.006) | head | 0.069 (0.048..0.090) | 1.269x | +0.015 (0.015) | +0.000 | reported | - |
| canvas-lower | cpu:CanvasToFramebuffer | 0.168 (0.153..0.184, 0.011) | head | 0.166 (0.110..0.212) | 0.992x | -0.001 (0.036) | -0.036 | reported | - |
| canvas-lower | cpu:ComputeSunShadow | 0.022 (0.018..0.025, 0.003) | head | 0.020 (0.014..0.026) | 0.893x | -0.002 (0.004) | -0.006 | reported | - |
| canvas-lower | cpu:LightingToTrixel | 0.062 (0.049..0.067, 0.007) | head | 0.055 (0.038..0.072) | 0.890x | -0.007 (0.012) | -0.018 | reported | - |
| canvas-lower | cpu:SingleVoxelToCanvasFirst | 0.314 (0.288..0.345, 0.022) | head | 0.333 (0.223..0.433) | 1.063x | +0.020 (0.072) | -0.049 | reported | - |
| canvas-lower | gpu:bakeSunShadowMap | 1.451 (1.434..1.465, 0.013) | head | 4.762 (4.549..4.827) | 3.282x | +3.311 (0.125) | +3.191 | delta <= 0.064 ms | FAIL |
| canvas-lower | gpu:canvasClear | 0.029 (0.029..0.029, 0.000) | head | 0.029 (0.028..0.030) | 1.000x | -0.000 (0.001) | -0.001 | reported | - |
| canvas-lower | gpu:cardinalSurfaceFb | 1.860 (1.762..1.933, 0.086) | head | 5.144 (5.063..5.327) | 2.766x | +3.284 (0.119) | +3.171 | reported | - |
| canvas-lower | gpu:computeLightVolume | 0.385 (0.350..0.429, 0.032) | head | 2.618 (2.213..3.220) | 6.807x | +2.233 (0.394) | +1.858 | reported | - |
| canvas-lower | gpu:computeSunShadow | 0.047 (0.041..0.053, 0.006) | head | 0.054 (0.046..0.071) | 1.140x | +0.007 (0.011) | -0.004 | reported | - |
| canvas-lower | gpu:computeVoxelAO | 0.081 (0.072..0.088, 0.007) | head | 0.271 (0.243..0.294) | 3.360x | +0.190 (0.030) | +0.162 | reported | - |
| canvas-lower | gpu:fbToScreen | 0.123 (0.122..0.124, 0.001) | head | 0.120 (0.116..0.121) | 0.977x | -0.003 (0.002) | -0.005 | reported | - |
| canvas-lower | gpu:lightingToTrixel | 0.052 (0.048..0.056, 0.004) | head | 0.051 (0.048..0.057) | 0.981x | -0.001 (0.003) | -0.004 | reported | - |
| canvas-lower | gpu:trixelToFb | 1.624 (1.541..1.686, 0.072) | head | 4.656 (4.540..4.812) | 2.866x | +3.032 (0.123) | +2.915 | reported | - |
| canvas-lower | gpu:voxelCardinalElect | 0.215 (0.211..0.221, 0.004) | head | 0.124 (0.114..0.138) | 0.578x | -0.091 (0.009) | -0.100 | reported | - |
| canvas-lower | gpu:voxelCompact | 0.088 (0.057..0.111, 0.028) | head | 0.080 (0.057..0.122) | 0.916x | -0.007 (0.026) | -0.032 | reported | - |
| canvas-lower | gpu:voxelStage1 | 0.085 (0.080..0.089, 0.004) | head | 0.083 (0.079..0.088) | 0.969x | -0.003 (0.004) | -0.006 | reported | - |
| canvas-lower | gpu:voxelStage2 | 0.215 (0.213..0.219, 0.003) | head | 0.180 (0.164..0.205) | 0.837x | -0.035 (0.018) | -0.052 | reported | - |
| canvas-lower | gpu_envelope_ms | 2.374 (2.265..2.453, 0.088) | head | 5.677 (5.555..5.865) | 2.391x | +3.303 (0.135) | +3.174 | reported | - |
| canvas-lower | steady_mean_ms | 8.850 (8.330..9.490, 0.527) | head | 16.754 (12.470..20.620) | 1.893x | +7.904 (3.356) | +4.704 | ratio <= 1.10x | FAIL |
| canvas-lower | steady_p99_ms | 14.140 (8.800..18.020, 4.868) | head | 23.250 (22.420..23.820) | 1.644x | +9.110 (4.504) | +4.815 | ratio <= 1.10x | FAIL |

| cell | arm | steady frame-time bins (share of frames) |
|---|---|---|
| canvas-lower | baseline | 7..8 ms 31%, 8..9 ms 63%, 16..17 ms 1%, 17..18 ms 4% |
| canvas-lower | per-pixel | 7..8 ms 6%, 8..9 ms 41%, 17..18 ms 26%, 18..19 ms 17% |
| canvas-lower | fills | 7..8 ms 5%, 8..9 ms 44%, 17..18 ms 22%, 18..19 ms 18% |
| canvas-lower | head | 8..9 ms 29%, 19..20 ms 21%, 20..21 ms 24%, 22..23 ms 7% |
