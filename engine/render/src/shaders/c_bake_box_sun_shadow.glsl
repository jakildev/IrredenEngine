#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#include "ir_iso_common.glsl"
#include "ir_sdf_common.glsl"
#include "ir_shape_data.glsl"
#include "ir_shape_receiver.glsl"
#include "ir_shape_sun_ray.glsl"
#include "ir_sun_projection.glsl"
layout(rg32ui, binding = 4) readonly uniform uimage2D fogShadowWindow;
#include "ir_fog_shadow_caster.glsl"
#include "ir_sun_face_query_layout.glsl"

layout(std430, binding = 20) readonly buffer ShapeBuffer { ShapeDescriptor shapes[]; };
layout(std140, binding = 23) uniform ShapeProjectionFrame { ShapeProjectionData shapeProjection; };
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
    uniform vec4 sunCasterViewToWorld;  // Unused here; places fogCeilingEnabled at 144
    uniform ivec4 fogCeilingEnabled;
};

#define IR_SUN_FACE_INDEX_COOPERATIVE
shared uint sharedFaceIndex;
#include "ir_sun_face_index.glsl"

void main() {
    const uint index = gl_WorkGroupID.y * uint(dispatch.y) + gl_WorkGroupID.x;
    if (index >= uint(dispatch.x)) return;
    const ShapeDescriptor shape = shapes[index];
    const bool box = shape.shapeType == SHAPE_BOX;
    if ((!box && !shapeUsesSunRay(shape, shapeProjection.voxelRenderOptions,
                                  shapeProjection.smoothYawEnabled, shapeProjection.latticeShapes)) ||
        (shape.flags & 8u) == 0u || (shape.color >> 24u) == 0u) return;
    const float threshold = 0.5 / float(dispatch.z);
    // BOX params count voxel centers; the SDF boundary adds half a raster cell.
    const vec3 halfExtent = box
        ? (shape.params.xyz - 1.0) * 0.5 + vec3(threshold)
        : shapeSunHalfExtent(shape.shapeType, shape.params, threshold);
    if (any(lessThanEqual(halfExtent, vec3(0.0)))) return;
    const vec4 rotation = shapeHasEntityRotation(shape.rotation)
        ? shape.rotation : vec4(0, 0, 0, 1);
    const vec3 axisX = rotateByQuat(vec3(1, 0, 0), rotation);
    const vec3 axisY = rotateByQuat(vec3(0, 1, 0), rotation);
    const vec3 axisZ = rotateByQuat(vec3(0, 0, 1), rotation);
    // Faces and footprint follow the drawn shape, not the raw descriptor position.
    const vec3 shapeCenter = shapeRenderedCenter(shape.worldPosition.xyz, shapeProjection);
    const vec3 center = sunSpaceProject(shapeCenter, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
    const vec3 extent = abs(sunSpaceProject(axisX, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.x
        + abs(sunSpaceProject(axisY, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.y
        + abs(sunSpaceProject(axisZ, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz)) * halfExtent.z;
    const vec3 direction = -vec3(dot(sunDirection.xyz, axisX), dot(sunDirection.xyz, axisY), dot(sunDirection.xyz, axisZ));
    const bool fogBody = (shape.flags & 16u) != 0u;
    const bool indexWholeShape = fogBody || fogCeilingEnabled.x == 0;
    // Ceiling-cut FIELD samples have no whole-shape record. Keep them in the
    // independent depth layer, which complete exact queries still consult.
    const uint depthLayerOffset = indexWholeShape ? kSourceFaceFallbackOffset : 0u;
    if (box && gl_WorkGroupID.z == 0u && indexWholeShape) {
        for (int axis = 0; axis < 3; ++axis) {
            vec3 localNormal = vec3(0.0);
            localNormal[axis] = 1.0;
            const bool positive = dot(rotateByQuat(localNormal, rotation), sunDirection.xyz) > 0.0;
            vec3 corner = -halfExtent;
            corner[axis] = positive ? halfExtent[axis] : -halfExtent[axis];
            vec3 edgeU = vec3(0.0), edgeV = vec3(0.0);
            edgeU[(axis + 1) % 3] = 2.0 * halfExtent[(axis + 1) % 3];
            edgeV[(axis + 2) % 3] = 2.0 * halfExtent[(axis + 2) % 3];
            corner = shapeCenter + rotateByQuat(corner, rotation);
            edgeU = rotateByQuat(edgeU, rotation);
            edgeV = rotateByQuat(edgeV, rotation);
            const vec3 projected = sunSpaceProject(corner, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            const vec3 projectedU = sunSpaceProject(edgeU, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            const vec3 projectedV = sunSpaceProject(edgeV, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
            indexSourceSunFace(projected, projectedU, projectedV);
        }
    }
    if (!box && gl_WorkGroupID.z == 0u && indexWholeShape) {
        const vec3 projectedAxis = sunSpaceProject(axisZ, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
        const vec3 parameters = vec3(shape.params.x, shape.shapeType == SHAPE_TORUS ? shape.params.y : shape.params.z, threshold);
        indexSourceSunGeometry(center, projectedAxis, parameters, shape.shapeType, center.xy - extent.xy, center.xy + extent.xy);
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
            const vec3 worldDelta = sunBasisU.xyz * uv.x + sunBasisV.xyz * uv.y - shapeCenter;
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
            float casterDepth = nearDepth;
            if (hit && nearDepth <= farDepth &&
                (box || sdfRayHit(shape.shapeType, shape.params, rayOrigin, direction, threshold, nearDepth, farDepth, casterDepth))) {
                const vec3 worldSample = sunBasisU.xyz * uv.x + sunBasisV.xyz * uv.y -
                    sunDirection.xyz * casterDepth;
                if (!(fogBody) && fogCeilingEnabled.x != 0 && fogShadowVisionCircleCount > 0 &&
        !fogFieldCastsSunShadow(worldSample, false, fogCeilingEnabled.x,
            fogShadowCell(worldSample))) continue;
                atomicMin(sunDepthBuf[depthLayerOffset + cascade * kCascadeTexelCount + pixel.y * kSunShadowMapDim + pixel.x], packSunSurfaceDepth(casterDepth));
            }
        }
    }
}
