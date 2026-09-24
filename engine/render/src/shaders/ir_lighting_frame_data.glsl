layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    uniform int   lightingEnabled;
    uniform int   lutEnabled;
    uniform int   lightVolumeEnabled;
    uniform float debugLightLevel;
    uniform int   debugOverlayMode;
    uniform int   hdrEnabled;
    uniform float exposure;
    uniform float skyIntensity;
    uniform vec4  skyColor;
    uniform vec4  detachedViewToWorld;
    uniform ivec4 normalOptions;
};
