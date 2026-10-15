#include "ir_iso_common.metal"
#include "ir_sdf_common.metal"
#include "ir_shape_data.metal"
#include "ir_shape_receiver.metal"
#include "ir_shape_sun_ray.metal"
#include "ir_sun_projection.metal"
#include "ir_sun_shadow_sample.metal"
#include "ir_fog_shadow_caster.metal"
#include <metal_atomic>

#define IR_SUN_FACE_INDEX_COOPERATIVE
#include "ir_sun_face_index.metal"

kernel void c_bake_box_sun_shadow(
    device const ShapeDescriptor* shapes [[buffer(20)]],
    device atomic_uint* sunDepthBuf [[buffer(28)]],
    constant int4& dispatch [[buffer(16)]],
    constant FrameDataSun& sunFrame [[buffer(29)]],
    constant ShapeProjectionData& shapeProjection [[buffer(23)]],
    constant FogShadowObserverData& fogObservers [[buffer(27)]],
    texture2d<uint, access::read> fogShadowWindow [[texture(4)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    threadgroup uint sharedFaceIndex;
    const uint index = groupId.y * uint(dispatch.y) + groupId.x;
    if (index >= uint(dispatch.x)) return;
    const ShapeDescriptor shape = shapes[index];
    const bool box = shape.shapeType == SHAPE_BOX;
    if ((!box && !shapeUsesSunRay(shape, shapeProjection.voxelRenderOptions,
                                  shapeProjection.smoothYawEnabled, shapeProjection.latticeShapes)) ||
        (shape.flags & 8u) == 0u || (shape.color >> 24u) == 0u) return;
    const float threshold = 0.5 / float(dispatch.z);
    // BOX params count voxel centers; the SDF boundary adds half a raster cell.
    const float3 halfExtent = box
        ? (shape.params.xyz - 1.0) * 0.5 + float3(threshold)
        : shapeSunHalfExtent(shape.shapeType, shape.params, threshold);
    if (any(halfExtent <= float3(0.0))) return;
    const float4 rotation = shapeHasEntityRotation(shape.rotation)
        ? shape.rotation : float4(0, 0, 0, 1);
    const float3 axisX = rotateByQuat(float3(1, 0, 0), rotation);
    const float3 axisY = rotateByQuat(float3(0, 1, 0), rotation);
    const float3 axisZ = rotateByQuat(float3(0, 0, 1), rotation);
    // Faces and footprint follow the drawn shape, not the raw descriptor position.
    const float3 shapeCenter = shapeRenderedCenter(shape.worldPosition.xyz, shapeProjection);
    const float3 center = sunSpaceProject(shapeCenter, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
    const float3 extent = abs(sunSpaceProject(axisX, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.x
        + abs(sunSpaceProject(axisY, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.y
        + abs(sunSpaceProject(axisZ, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.z;
    const float3 direction = -float3(dot(sunFrame.sunDirection.xyz, axisX), dot(sunFrame.sunDirection.xyz, axisY), dot(sunFrame.sunDirection.xyz, axisZ));
    const bool fogBody = (shape.flags & 16u) != 0u;
    const bool indexWholeShape = fogBody || sunFrame.fogCeilingEnabled.x == 0;
    // Ceiling-cut FIELD samples have no whole-shape record. Keep them in the
    // independent depth layer, which complete exact queries still consult.
    const uint depthLayerOffset = indexWholeShape ? kSourceFaceFallbackOffset : 0u;
    if (box && groupId.z == 0u && indexWholeShape) {
        for (int axis = 0; axis < 3; ++axis) {
            float3 localNormal = float3(0.0);
            localNormal[axis] = 1.0;
            const bool positive = dot(rotateByQuat(localNormal, rotation), sunFrame.sunDirection.xyz) > 0.0;
            float3 corner = -halfExtent;
            corner[axis] = positive ? halfExtent[axis] : -halfExtent[axis];
            float3 edgeU = float3(0.0), edgeV = float3(0.0);
            edgeU[(axis + 1) % 3] = 2.0 * halfExtent[(axis + 1) % 3];
            edgeV[(axis + 2) % 3] = 2.0 * halfExtent[(axis + 2) % 3];
            corner = shapeCenter + rotateByQuat(corner, rotation);
            edgeU = rotateByQuat(edgeU, rotation);
            edgeV = rotateByQuat(edgeV, rotation);
            const float3 projected = sunSpaceProject(corner, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
            const float3 projectedU = sunSpaceProject(edgeU, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
            const float3 projectedV = sunSpaceProject(edgeV, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
            indexSourceSunFace(sunDepthBuf, projected, projectedU, projectedV, sunFrame, localId.x, sharedFaceIndex);
        }
    }
    if (!box && groupId.z == 0u && indexWholeShape) {
        const float3 projectedAxis = sunSpaceProject(axisZ, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        const float3 parameters = float3(shape.params.x, shape.shapeType == SHAPE_TORUS ? shape.params.y : shape.params.z, threshold);
        indexSourceSunGeometry(sunDepthBuf, center, projectedAxis, parameters, shape.shapeType, center.xy - extent.xy, center.xy + extent.xy, sunFrame, localId.x, sharedFaceIndex);
    }
    for (int cascade = 0; cascade < 2; ++cascade) {
        const float2 origin = cascade == 0 ? sunFrame.cascadeOriginUV_0 : sunFrame.cascadeOriginUV_1;
        const float2 texel = cascade == 0 ? sunFrame.cascadeTexelSize_0 : sunFrame.cascadeTexelSize_1;
        const int2 first = max(int2(ceil((center.xy - extent.xy - origin) / texel - 0.5)), int2(0));
        const int2 last = min(int2(floor((center.xy + extent.xy - origin) / texel - 0.5)), int2(kSunShadowMapDim - 1));
        if (any(first > last)) continue;
        const int2 size = last - first + 1;
        for (uint sampleIndex = groupId.z * 64u + localId.x; sampleIndex < uint(size.x * size.y); sampleIndex += 64u * uint(dispatch.w)) {
            const int2 pixel = first + int2(int(sampleIndex) % size.x, int(sampleIndex) / size.x);
            const float2 uv = origin + (float2(pixel) + 0.5) * texel;
            const float3 worldDelta = sunFrame.sunBasisU.xyz * uv.x + sunFrame.sunBasisV.xyz * uv.y - shapeCenter;
            const float3 rayOrigin = float3(dot(worldDelta, axisX), dot(worldDelta, axisY), dot(worldDelta, axisZ));
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
                const float3 worldSample = sunFrame.sunBasisU.xyz * uv.x +
                    sunFrame.sunBasisV.xyz * uv.y - sunFrame.sunDirection.xyz * casterDepth;
                if (!(fogBody) && sunFrame.fogCeilingEnabled.x != 0 && fogObservers.visionCircleCount > 0 &&
        !fogFieldCastsSunShadow(worldSample, false, sunFrame.fogCeilingEnabled.x,
            fogShadowCell(worldSample, fogObservers, fogShadowWindow), fogObservers)) continue;
                atomic_fetch_min_explicit(&sunDepthBuf[depthLayerOffset + cascade * kCascadeTexelCount + pixel.y * kSunShadowMapDim + pixel.x], packSunSurfaceDepth(casterDepth), memory_order_relaxed);
            }
        }
    }
}
