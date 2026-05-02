#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D screenTexture;
layout (binding = 1) uniform sampler2D depthTexture;

layout (std140, binding = 16) uniform FrameData {
    mat4 mvpMatrix;
    vec2 textureOffset;
    float residualYaw;
    float _pad0;
};

// kIdentityYawEpsilon is the |residualYaw| threshold below which we treat
// the rotation as identity and bypass the bilinear blend so the output is
// pixel-identical to the source. Tighter than the camera yaw normalization
// epsilon so cardinal-snap angles always land in the passthrough branch.
const float kIdentityYawEpsilon = 1e-6;

// Manual bilinear sample: the framebuffer color/depth textures are created
// with TextureFilter::NEAREST and TextureWrap::REPEAT (see Framebuffer
// ctor in framebuffer.cpp). Each tap clamps to [edge, 1-edge] in texel
// space so the rotated sample doesn't wrap content from the opposite side
// of the texture; the Metal sampler does this implicitly via
// address::clamp_to_edge.
vec4 sampleBilinearClamped(sampler2D tex, vec2 uv) {
    vec2 ts = vec2(textureSize(tex, 0));
    vec2 pixel = uv * ts - vec2(0.5);
    vec2 floorPixel = floor(pixel);
    vec2 frac = clamp(pixel - floorPixel, vec2(0.0), vec2(1.0));
    vec2 maxIdx = ts - vec2(1.0);
    vec2 c00 = (clamp(floorPixel, vec2(0.0), maxIdx) + vec2(0.5)) / ts;
    vec2 c10 = (clamp(floorPixel + vec2(1.0, 0.0), vec2(0.0), maxIdx) + vec2(0.5)) / ts;
    vec2 c01 = (clamp(floorPixel + vec2(0.0, 1.0), vec2(0.0), maxIdx) + vec2(0.5)) / ts;
    vec2 c11 = (clamp(floorPixel + vec2(1.0, 1.0), vec2(0.0), maxIdx) + vec2(0.5)) / ts;
    vec4 t00 = texture(tex, c00);
    vec4 t10 = texture(tex, c10);
    vec4 t01 = texture(tex, c01);
    vec4 t11 = texture(tex, c11);
    return mix(mix(t00, t10, frac.x), mix(t01, t11, frac.x), frac.y);
}

void main() {
    if (abs(residualYaw) < kIdentityYawEpsilon) {
        FragColor = texture(screenTexture, TexCoords);
        gl_FragDepth = texture(depthTexture, TexCoords).r;
        return;
    }

    vec2 ts = vec2(textureSize(screenTexture, 0));
    vec2 pixelPos = TexCoords * ts;
    vec2 center = ts * 0.5;
    vec2 centered = pixelPos - center;

    // Inverse rotation: each output pixel samples from the source position
    // it would arrive at after a +residualYaw rotation, which is the
    // current position rotated by -residualYaw.
    float c = cos(-residualYaw);
    float s = sin(-residualYaw);
    vec2 rotated = vec2(c * centered.x - s * centered.y,
                        s * centered.x + c * centered.y);
    vec2 sampleUV = (rotated + center) / ts;

    FragColor = sampleBilinearClamped(screenTexture, sampleUV);
    gl_FragDepth = sampleBilinearClamped(depthTexture, sampleUV).r;
}
