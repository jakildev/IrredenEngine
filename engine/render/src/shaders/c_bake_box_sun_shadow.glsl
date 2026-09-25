#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#include "ir_iso_common.glsl"
#include "ir_sun_projection.glsl"
#include "ir_sun_face_query_layout.glsl"

struct ShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    vec4 rotation;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

layout(std430, binding = 20) readonly buffer ShapeBuffer { ShapeDescriptor shapes[]; };
layout(std430, binding = 28) restrict buffer SunShadowDepthMap { uint sunDepthBuf[]; };
layout(std140, binding = 16) uniform ShapeSunFrame { ivec4 dispatch; };
layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float sunSplatMaxTexels;
    uniform float sunMaxShadowThrow;  // Unused here (receiver-only)
};

#include "ir_sun_face_index.glsl"

void main() {
    const uint index = gl_WorkGroupID.y * uint(dispatch.y) + gl_WorkGroupID.x;
    if (index >= uint(dispatch.x)) return;
    const ShapeDescriptor shape = shapes[index];
    if (shape.shapeType != 0u || (shape.flags & 8u) == 0u || (shape.color >> 24u) == 0u) return;
    // BOX params count voxel centers; the SDF boundary adds half a raster cell.
    const vec3 halfExtent = (shape.params.xyz - 1.0) * 0.5 + vec3(0.5 / float(dispatch.z));
    if (any(lessThanEqual(halfExtent, vec3(0.0)))) return;
    const vec3 axisX = rotateByQuat(vec3(1, 0, 0), shape.rotation);
    const vec3 axisY = rotateByQuat(vec3(0, 1, 0), shape.rotation);
    const vec3 axisZ = rotateByQuat(vec3(0, 0, 1), shape.rotation);
    const vec3 center = sunSpaceProject(shape.worldPosition.xyz, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
    const vec3 extent = abs(sunSpaceProject(axisX, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.x
        + abs(sunSpaceProject(axisY, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.y
        + abs(sunSpaceProject(axisZ, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.z;
    const vec3 direction = -vec3(dot(sunDirection.xyz, axisX), dot(sunDirection.xyz, axisY), dot(sunDirection.xyz, axisZ));
    if (gl_WorkGroupID.z == 0u && gl_LocalInvocationID.x == 0u) {
        for (int axis = 0; axis < 3; ++axis) {
            vec3 localNormal = vec3(0.0);
            localNormal[axis] = 1.0;
            const bool positive = dot(rotateByQuat(localNormal, shape.rotation), sunDirection.xyz) > 0.0;
            vec3 corner = -halfExtent;
            corner[axis] = positive ? halfExtent[axis] : -halfExtent[axis];
            vec3 edgeU = vec3(0.0), edgeV = vec3(0.0);
            edgeU[(axis + 1) % 3] = 2.0 * halfExtent[(axis + 1) % 3];
            edgeV[(axis + 2) % 3] = 2.0 * halfExtent[(axis + 2) % 3];
            corner = shape.worldPosition.xyz + rotateByQuat(corner, shape.rotation);
            edgeU = rotateByQuat(edgeU, shape.rotation);
            edgeV = rotateByQuat(edgeV, shape.rotation);
            const vec3 projected = sunSpaceProject(corner, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            const vec3 projectedU = sunSpaceProject(edgeU, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            const vec3 projectedV = sunSpaceProject(edgeV, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            indexSourceSunFace(projected, projectedU, projectedV);
        }
    }
    for (int cascade = 0; cascade < 2; ++cascade) {
        const vec2 origin = cascade == 0 ? cascadeOriginUV_0 : cascadeOriginUV_1;
        const vec2 texel = cascade == 0 ? cascadeTexelSize_0 : cascadeTexelSize_1;
        const ivec2 first = max(ivec2(ceil((center.xy - extent.xy - origin) / texel - 0.5)), ivec2(0));
        const ivec2 last = min(ivec2(floor((center.xy + extent.xy - origin) / texel - 0.5)), ivec2(kSunShadowMapDim - 1));
        if (any(greaterThan(first, last))) continue;
        const ivec2 size = last - first + 1;
        for (uint sampleIndex = gl_WorkGroupID.z * 64u + gl_LocalInvocationID.x; sampleIndex < uint(size.x * size.y); sampleIndex += 64u * uint(dispatch.w)) {
            const ivec2 pixel = first + ivec2(int(sampleIndex) % size.x, int(sampleIndex) / size.x);
            const vec2 uv = origin + (vec2(pixel) + 0.5) * texel;
            const vec3 worldDelta = sunBasisU.xyz * uv.x + sunBasisV.xyz * uv.y - shape.worldPosition.xyz;
            const vec3 rayOrigin = vec3(dot(worldDelta, axisX), dot(worldDelta, axisY), dot(worldDelta, axisZ));
            float nearDepth = -1e30;
            float farDepth = 1e30;
            bool hit = true;
            for (int axis = 0; axis < 3; ++axis) {
                if (abs(direction[axis]) < 1e-7) {
                    if (abs(rayOrigin[axis]) > halfExtent[axis]) hit = false;
                } else {
                    const float a = (-halfExtent[axis] - rayOrigin[axis]) / direction[axis];
                    const float b = (halfExtent[axis] - rayOrigin[axis]) / direction[axis];
                    nearDepth = max(nearDepth, min(a, b));
                    farDepth = min(farDepth, max(a, b));
                }
            }
            if (hit && nearDepth <= farDepth) {
                atomicMin(sunDepthBuf[kSourceFaceFallbackOffset + cascade * kCascadeTexelCount + pixel.y * kSunShadowMapDim + pixel.x], packSunSurfaceDepth(nearDepth));
            }
        }
    }
}
