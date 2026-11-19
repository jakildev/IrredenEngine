| Case / row | n | parent mean ms | head mean ms | head/parent | parent range | head range | paired Δ mean ± sd | one-sided 95% lower | rule |
|---|---:|---:|---:|---:|---|---|---:|---:|---|
| profiling-on-yaw0 frame | 5 | 18.436 | 18.588 | 1.008 | 18.420–18.450 | 18.560–18.620 | +0.152 ± 0.033 | +0.121 | PASS(i) |
| profiling-on-yaw0 steady | 5 | 18.426 | 18.584 | 1.009 | 18.400–18.440 | 18.550–18.610 | +0.158 ± 0.023 | +0.136 | PASS(i) |
| profiling-on-yaw45 frame | 5 | 10.886 | 11.286 | 1.037 | 10.820–10.960 | 11.230–11.350 | +0.400 ± 0.087 | +0.317 | PASS(i) |
| profiling-on-yaw45 steady | 5 | 10.862 | 11.270 | 1.038 | 10.820–10.930 | 11.210–11.340 | +0.408 ± 0.065 | +0.346 | PASS(i) |
| profiling-on-yawsweep frame | 5 | 11.852 | 12.594 | 1.063 | 11.820–11.880 | 12.520–12.700 | +0.742 ± 0.084 | +0.662 | PASS(i) |
| profiling-on-yawsweep steady | 5 | 12.094 | 12.800 | 1.058 | 12.080–12.110 | 12.720–12.910 | +0.706 ± 0.081 | +0.629 | PASS(i) |
| profiling-off-yaw0 frame | 5 | 18.210 | 18.336 | 1.007 | 18.160–18.230 | 18.310–18.360 | +0.126 ± 0.038 | +0.089 | PASS(i) |
| profiling-off-yaw0 steady | 5 | 18.208 | 18.336 | 1.007 | 18.170–18.240 | 18.310–18.380 | +0.128 ± 0.034 | +0.095 | PASS(i) |
| profiling-off-yaw45 frame | 5 | 10.496 | 10.954 | 1.044 | 10.480–10.510 | 10.910–11.000 | +0.458 ± 0.041 | +0.419 | PASS(i) |
| profiling-off-yaw45 steady | 5 | 10.490 | 10.952 | 1.044 | 10.470–10.510 | 10.900–11.010 | +0.462 ± 0.053 | +0.412 | PASS(i) |
| profiling-off-yawsweep frame | 5 | 11.490 | 12.200 | 1.062 | 11.450–11.580 | 12.190–12.210 | +0.710 ± 0.060 | +0.653 | PASS(i) |
| profiling-off-yawsweep steady | 5 | 11.742 | 12.414 | 1.057 | 11.700–11.820 | 12.400–12.430 | +0.672 ± 0.061 | +0.613 | PASS(i) |

GPU stage rows (route = present on one arm only):

| profiling-on-yaw0 bakeSunShadowMap | 5 | 0.099 | 0.099 | 0.996 | 0.099–0.100 | 0.098–0.101 | -0.000 ± 0.001 | -0.001 | PASS(i) |
| profiling-on-yaw0 canvasClear | 5 | 0.175 | 0.165 | 0.946 | 0.174–0.176 | 0.162–0.168 | -0.009 ± 0.002 | -0.011 | PASS(i) |
| profiling-on-yaw0 cardinalSurfaceFb | 5 | 0.000 | 17.445 | route |
| profiling-on-yaw0 computeLightVolume | 5 | 0.292 | 0.289 | 0.988 | 0.289–0.296 | 0.287–0.291 | -0.003 ± 0.004 | -0.007 | PASS(i) |
| profiling-on-yaw0 computeSunShadow | 5 | 0.263 | 0.015 | 0.058 | 0.262–0.264 | 0.015–0.016 | -0.248 ± 0.000 | -0.248 | PASS(i) |
| profiling-on-yaw0 computeVoxelAO | 5 | 0.063 | 0.063 | 1.003 | 0.063–0.064 | 0.063–0.064 | +0.000 ± 0.000 | -0.000 | PASS(i) |
| profiling-on-yaw0 fbToScreen | 5 | 0.028 | 0.029 | 1.036 | 0.028–0.028 | 0.029–0.029 | +0.001 ± 0.000 | +0.001 | PASS(i) |
| profiling-on-yaw0 fogToTrixel | 5 | 0.012 | 0.010 | 0.833 | 0.012–0.012 | 0.010–0.010 | -0.002 ± 0.000 | -0.002 | PASS(i) |
| profiling-on-yaw0 lightingToTrixel | 5 | 0.043 | 0.015 | 0.347 | 0.042–0.044 | 0.015–0.015 | -0.028 ± 0.001 | -0.029 | PASS(i) |
| profiling-on-yaw0 textToTrixel | 5 | 0.143 | 0.233 | 1.628 | 0.139–0.147 | 0.227–0.238 | +0.090 ± 0.007 | +0.083 | FAIL(i) |
| profiling-on-yaw0 trixelToFb | 5 | 0.264 | 0.245 | 0.929 | 0.256–0.269 | 0.240–0.250 | -0.019 ± 0.005 | -0.023 | PASS(i) |
| profiling-on-yaw0 voxelCardinalElect | 5 | 4.512 | 4.512 | 1.000 | 4.512–4.514 | 4.512–4.514 | +0.000 ± 0.001 | -0.001 | PASS(i) |
| profiling-on-yaw0 voxelCompact | 5 | 0.219 | 0.194 | 0.886 | 0.211–0.223 | 0.187–0.199 | -0.025 ± 0.008 | -0.033 | PASS(i) |
| profiling-on-yaw0 voxelStage1 | 5 | 4.436 | 4.436 | 1.000 | 4.435–4.436 | 4.436–4.437 | +0.001 ± 0.001 | +0.000 | PASS(i) |
| profiling-on-yaw0 voxelStage2 | 5 | 4.426 | 4.426 | 1.000 | 4.426–4.426 | 4.426–4.427 | +0.000 ± 0.001 | -0.000 | PASS(i) |
| profiling-on-yaw0 voxelSunFaces | 5 | 3.195 | 3.191 | 0.999 | 3.194–3.196 | 3.189–3.195 | -0.004 ± 0.002 | -0.006 | PASS(i) |
| profiling-on-yaw45 bakeSunShadowMap | 5 | 0.099 | 0.099 | 0.996 | 0.099–0.100 | 0.098–0.099 | -0.000 ± 0.001 | -0.001 | PASS(i) |
| profiling-on-yaw45 canvasClear | 5 | 0.170 | 0.175 | 1.028 | 0.167–0.174 | 0.172–0.177 | +0.005 ± 0.002 | +0.003 | PASS(i) |
| profiling-on-yaw45 computeLightVolume | 5 | 0.228 | 0.323 | 1.417 | 0.226–0.230 | 0.320–0.324 | +0.095 ± 0.003 | +0.092 | FAIL(i) |
| profiling-on-yaw45 computeSunShadow | 5 | 0.593 | 0.031 | 0.053 | 0.589–0.602 | 0.031–0.032 | -0.562 ± 0.006 | -0.568 | PASS(i) |
| profiling-on-yaw45 computeVoxelAO | 5 | 0.014 | 0.014 | 0.986 | 0.014–0.014 | 0.013–0.014 | -0.000 ± 0.000 | -0.001 | PASS(i) |
| profiling-on-yaw45 computeVoxelAoPerAxis | 5 | 0.046 | 0.045 | 0.983 | 0.045–0.047 | 0.044–0.046 | -0.001 ± 0.001 | -0.002 | PASS(i) |
| profiling-on-yaw45 fbToScreen | 5 | 0.028 | 0.029 | 1.021 | 0.028–0.028 | 0.028–0.029 | +0.001 ± 0.001 | +0.000 | PASS(i) |
| profiling-on-yaw45 fogOverflow | 5 | 0.009 | 0.000 | route |
| profiling-on-yaw45 fogPerAxis | 5 | 0.023 | 0.000 | route |
| profiling-on-yaw45 fogToTrixel | 5 | 0.010 | 0.010 | 1.000 | 0.010–0.010 | 0.010–0.010 | +0.000 ± 0.000 | +0.000 | PASS(i) |
| profiling-on-yaw45 lightingOverflow | 5 | 0.118 | 0.023 | 0.197 | 0.117–0.119 | 0.023–0.024 | -0.095 ± 0.000 | -0.095 | PASS(i) |
| profiling-on-yaw45 lightingPerAxis | 5 | 0.034 | 0.000 | route |
| profiling-on-yaw45 lightingToTrixel | 5 | 0.015 | 0.016 | 1.040 | 0.015–0.015 | 0.015–0.016 | +0.001 ± 0.001 | +0.000 | PASS(i) |
| profiling-on-yaw45 perAxisCellCompact | 5 | 0.072 | 0.073 | 1.008 | 0.072–0.073 | 0.072–0.073 | +0.001 ± 0.001 | +0.000 | PASS(i) |
| profiling-on-yaw45 perAxisScatter | 5 | 0.590 | 0.000 | route |
| profiling-on-yaw45 perAxisSurfaceFb | 5 | 0.000 | 1.796 | route |
| profiling-on-yaw45 textToTrixel | 5 | 0.056 | 0.066 | 1.183 | 0.055–0.056 | 0.065–0.068 | +0.010 ± 0.001 | +0.009 | FAIL(i) |
| profiling-on-yaw45 trixelToFb | 5 | 0.056 | 0.065 | 1.164 | 0.055–0.058 | 0.060–0.070 | +0.009 ± 0.003 | +0.006 | FAIL(i) |
| profiling-on-yaw45 voxelCompact | 5 | 0.278 | 0.215 | 0.772 | 0.268–0.283 | 0.204–0.225 | -0.063 ± 0.004 | -0.067 | PASS(i) |
| profiling-on-yaw45 voxelPerAxisFinalize | 5 | 1.340 | 1.341 | 1.001 | 1.339–1.341 | 1.340–1.343 | +0.001 ± 0.002 | -0.000 | PASS(i) |
| profiling-on-yaw45 voxelPerAxisOverflow | 5 | 2.444 | 2.318 | 0.949 | 2.442–2.446 | 2.315–2.322 | -0.125 ± 0.004 | -0.129 | PASS(i) |
| profiling-on-yaw45 voxelPerAxisStore | 5 | 0.732 | 0.740 | 1.011 | 0.729–0.736 | 0.739–0.741 | +0.008 ± 0.003 | +0.005 | PASS(i) |
| profiling-on-yaw45 voxelSunFaces | 5 | 2.818 | 2.809 | 0.997 | 2.816–2.819 | 2.806–2.812 | -0.009 ± 0.002 | -0.011 | PASS(i) |
| profiling-on-yawsweep bakeSunShadowMap | 5 | 0.099 | 0.099 | 1.002 | 0.098–0.099 | 0.098–0.100 | +0.000 ± 0.001 | -0.001 | PASS(i) |
| profiling-on-yawsweep canvasClear | 5 | 0.170 | 0.181 | 1.067 | 0.169–0.171 | 0.179–0.186 | +0.011 ± 0.003 | +0.009 | PASS(i) |
| profiling-on-yawsweep cardinalSurfaceFb | 5 | 0.000 | 19.713 | route |
| profiling-on-yawsweep computeLightVolume | 5 | 0.232 | 0.321 | 1.380 | 0.230–0.234 | 0.320–0.322 | +0.088 ± 0.002 | +0.086 | FAIL(i) |
| profiling-on-yawsweep computeSunShadow | 5 | 0.533 | 0.030 | 0.057 | 0.532–0.535 | 0.030–0.031 | -0.503 ± 0.001 | -0.504 | PASS(i) |
| profiling-on-yawsweep computeVoxelAO | 5 | 0.014 | 0.015 | 1.028 | 0.014–0.015 | 0.014–0.015 | +0.000 ± 0.001 | -0.000 | PASS(i) |
| profiling-on-yawsweep computeVoxelAoPerAxis | 5 | 0.046 | 0.046 | 1.018 | 0.045–0.047 | 0.046–0.047 | +0.001 ± 0.001 | -0.000 | PASS(i) |
| profiling-on-yawsweep fbToScreen | 5 | 0.028 | 0.029 | 1.036 | 0.028–0.028 | 0.029–0.029 | +0.001 ± 0.000 | +0.001 | PASS(i) |
| profiling-on-yawsweep fogOverflow | 5 | 0.009 | 0.000 | route |
| profiling-on-yawsweep fogPerAxis | 5 | 0.022 | 0.000 | route |
| profiling-on-yawsweep fogToTrixel | 5 | 0.010 | 0.010 | 1.000 | 0.010–0.010 | 0.010–0.010 | +0.000 ± 0.000 | +0.000 | PASS(i) |
| profiling-on-yawsweep lightingOverflow | 5 | 0.106 | 0.022 | 0.205 | 0.105–0.107 | 0.021–0.022 | -0.084 ± 0.001 | -0.085 | PASS(i) |
| profiling-on-yawsweep lightingPerAxis | 5 | 0.034 | 0.000 | route |
| profiling-on-yawsweep lightingToTrixel | 5 | 0.015 | 0.016 | 1.013 | 0.015–0.016 | 0.015–0.016 | +0.000 ± 0.000 | -0.000 | PASS(i) |
| profiling-on-yawsweep perAxisCellCompact | 5 | 0.072 | 0.073 | 1.006 | 0.072–0.073 | 0.072–0.074 | +0.000 ± 0.001 | -0.001 | PASS(i) |
| profiling-on-yawsweep perAxisScatter | 5 | 0.499 | 0.000 | route |
| profiling-on-yawsweep perAxisSurfaceFb | 5 | 0.000 | 1.828 | route |
| profiling-on-yawsweep textToTrixel | 5 | 0.057 | 0.069 | 1.215 | 0.056–0.057 | 0.067–0.072 | +0.012 ± 0.003 | +0.010 | FAIL(i) |
| profiling-on-yawsweep trixelToFb | 5 | 0.061 | 0.073 | 1.193 | 0.060–0.062 | 0.068–0.077 | +0.012 ± 0.004 | +0.008 | FAIL(i) |
| profiling-on-yawsweep voxelCardinalElect | 5 | 4.651 | 4.868 | 1.047 | 4.646–4.666 | 4.646–5.655 | +0.217 ± 0.442 | -0.204 | PASS(i) |
| profiling-on-yawsweep voxelCompact | 5 | 0.271 | 0.206 | 0.759 | 0.269–0.273 | 0.204–0.208 | -0.065 ± 0.003 | -0.068 | PASS(i) |
| profiling-on-yawsweep voxelPerAxisFinalize | 5 | 1.342 | 1.342 | 1.000 | 1.341–1.344 | 1.341–1.344 | +0.000 ± 0.002 | -0.001 | PASS(i) |
| profiling-on-yawsweep voxelPerAxisOverflow | 5 | 2.414 | 2.408 | 0.998 | 2.413–2.416 | 2.406–2.411 | -0.006 ± 0.002 | -0.008 | PASS(i) |
| profiling-on-yawsweep voxelPerAxisStore | 5 | 0.740 | 0.745 | 1.007 | 0.739–0.740 | 0.744–0.747 | +0.005 ± 0.002 | +0.004 | PASS(i) |
| profiling-on-yawsweep voxelStage1 | 5 | 5.208 | 5.427 | 1.042 | 5.135–5.257 | 5.098–6.434 | +0.218 ± 0.557 | -0.313 | PASS(i) |
| profiling-on-yawsweep voxelStage2 | 5 | 4.546 | 4.660 | 1.025 | 4.546–4.546 | 4.546–5.118 | +0.114 ± 0.256 | -0.130 | PASS(i) |
| profiling-on-yawsweep voxelSunFaces | 5 | 3.845 | 3.833 | 0.997 | 3.844–3.847 | 3.832–3.834 | -0.012 ± 0.001 | -0.013 | PASS(i) |

Surface-route stage each arm's reports sampled:

| Case | parent runs | head runs |
|---|---|---|
| profiling-on-yaw0 | none | cardinalSurfaceFb |
| profiling-on-yaw45 | none | perAxisSurfaceFb |
| profiling-on-yawsweep | none | cardinalSurfaceFb + perAxisSurfaceFb |
| profiling-off-yaw0 | untimed | untimed |
| profiling-off-yaw45 | untimed | untimed |
| profiling-off-yawsweep | untimed | untimed |
