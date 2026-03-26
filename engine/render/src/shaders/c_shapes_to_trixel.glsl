#version 460 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

layout(std140, binding = 23) uniform ShapesFrameData {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 canvasSize;
    uniform int shapeCount;
    uniform int passIndex;
    uniform ivec2 voxelRenderOptions;
};

struct ShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

struct JointTransform {
    vec4 rotation;
    vec4 translation;
    uint parentJointIndex;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct AnimParams {
    float time;
    float speed;
    float phase;
    float _pad0;
    vec4 blend;
};

layout(std430, binding = 20) readonly buffer ShapeBuffer {
    ShapeDescriptor shapes[];
};

layout(std430, binding = 21) readonly buffer JointBuffer {
    JointTransform joints[];
};

layout(std430, binding = 22) readonly buffer AnimBuffer {
    AnimParams animations[];
};

layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;
layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(rg32ui, binding = 2) writeonly uniform uimage2D triangleCanvasEntityIds;

const uint SHAPE_BOX = 0u;
const uint SHAPE_SPHERE = 1u;
const uint SHAPE_CYLINDER = 2u;
const uint SHAPE_ELLIPSOID = 3u;
const uint SHAPE_WING = 4u;
const uint SHAPE_PRISM = 5u;
const uint SHAPE_TAPERED_BOX = 6u;

const uint FLAG_HOLLOW = 1u;
const uint FLAG_VISIBLE = 8u;

const int kXFace = 0;
const int kYFace = 1;
const int kZFace = 2;

int localIDToFace() {
    if (gl_LocalInvocationID.y == 0) return kZFace;
    if (gl_LocalInvocationID.x == 1) return kXFace;
    return kYFace;
}

ivec2 pos3DtoPos2DIso(ivec3 p) {
    return ivec2(-p.x + p.y, -p.x - p.y + 2 * p.z);
}

int pos3DtoDistance(ivec3 p) {
    return p.x + p.y + p.z;
}

vec4 unpackColor(uint c) {
    return vec4(
        float(c & 0xFFu) / 255.0,
        float((c >> 8) & 0xFFu) / 255.0,
        float((c >> 16) & 0xFFu) / 255.0,
        float((c >> 24) & 0xFFu) / 255.0
    );
}

vec4 adjustColorForFace(vec4 col, int face) {
    float b = 1.0;
    if (face == kYFace) b = 0.75;
    if (face == kZFace) b = 1.25;
    return vec4(clamp(col.rgb * b, 0.0, 1.0), col.a);
}

float sdfBox(vec3 p, vec3 halfExtents) {
    vec3 d = abs(p) - halfExtents;
    return max(d.x, max(d.y, d.z));
}

float sdfSphere(vec3 p, float radius) {
    return length(p) - radius;
}

float sdfCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdfEllipsoid(vec3 p, vec3 radii) {
    float k0 = length(p / radii);
    float k1 = length(p / (radii * radii));
    return k0 * (k0 - 1.0) / k1;
}

float sdfTaperedBox(vec3 p, vec3 halfExtents, float taper) {
    float taperFactor = mix(1.0, taper, clamp((p.z + halfExtents.z) / (2.0 * halfExtents.z), 0.0, 1.0));
    vec3 scaled = vec3(p.xy / max(taperFactor, 0.001), p.z);
    return sdfBox(scaled, halfExtents);
}

float evaluateSDF(vec3 localPos, ShapeDescriptor shape) {
    vec3 halfSize = shape.params.xyz * 0.5;

    switch (shape.shapeType) {
        case SHAPE_BOX:
            return sdfBox(localPos, halfSize);
        case SHAPE_SPHERE:
            return sdfSphere(localPos, shape.params.x);
        case SHAPE_CYLINDER:
            return sdfCylinder(localPos, shape.params.x, halfSize.z);
        case SHAPE_ELLIPSOID:
            return sdfEllipsoid(localPos, halfSize);
        case SHAPE_TAPERED_BOX:
            return sdfTaperedBox(localPos, halfSize, shape.params.w);
        default:
            return sdfBox(localPos, halfSize);
    }
}

ivec3 faceMicroPositionFixed(int face, ivec3 voxelPositionFixed, int u, int v, int subdivisions) {
    if (face == kXFace) {
        return ivec3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (face == kYFace) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    return ivec3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z
    );
}

void writeTap(ivec2 pixel, int dist, vec4 col, uint entityId, int face) {
    if (pixel.x < 0 || pixel.x >= canvasSize.x ||
        pixel.y < 0 || pixel.y >= canvasSize.y) return;

    if (passIndex == 0) {
        imageAtomicMin(triangleCanvasDistances, pixel, dist);
    } else {
        int stored = imageLoad(triangleCanvasDistances, pixel).x;
        if (dist == stored) {
            imageStore(triangleCanvasColors, pixel, col);
            imageStore(triangleCanvasEntityIds, pixel,
                       uvec4(entityId, 0u, 0u, 0u));
        }
    }
}

void main() {
    uint shapeIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * 1024u;
    if (shapeIdx >= uint(shapeCount)) return;

    ShapeDescriptor shape = shapes[shapeIdx];
    if ((shape.flags & FLAG_VISIBLE) == 0u) return;

    vec4 col = unpackColor(shape.color);
    if (col.a == 0.0) return;

    int face = localIDToFace();
    col = adjustColorForFace(col, face);

    vec3 worldPos = shape.worldPosition.xyz;
    vec3 halfSize = shape.params.xyz * 0.5;
    ivec3 bboxMin = ivec3(floor(worldPos - halfSize - vec3(1.0)));
    ivec3 bboxMax = ivec3(ceil(worldPos + halfSize + vec3(1.0)));

    ivec2 faceLocalOffset = ivec2(gl_LocalInvocationID.xy);

    bool isSmooth = voxelRenderOptions.x != 0;
    int subdivisions = max(voxelRenderOptions.y, 1);

    for (int z = bboxMin.z; z <= bboxMax.z; ++z) {
        for (int y = bboxMin.y; y <= bboxMax.y; ++y) {
            for (int x = bboxMin.x; x <= bboxMax.x; ++x) {
                vec3 localPos = vec3(x, y, z) - worldPos;
                float d = evaluateSDF(localPos, shape);
                if (d > 0.5) continue;

                bool hollow = (shape.flags & FLAG_HOLLOW) != 0u;
                if (hollow && d < -0.5) continue;

                ivec3 vp = ivec3(x, y, z);

                if (isSmooth) {
                    ivec3 vpFixed = vp * subdivisions;
                    ivec2 frameOffsetFixed =
                        trixelCanvasOffsetZ1 +
                        ivec2(floor(frameCanvasOffset * float(subdivisions)));

                    for (int u = 0; u < subdivisions; ++u) {
                        for (int v = 0; v < subdivisions; ++v) {
                            ivec3 micro = faceMicroPositionFixed(
                                face, vpFixed, u, v, subdivisions);
                            int depthBase = micro.x + micro.y + micro.z;
                            int dist = depthBase * 4 + face;
                            ivec2 pixel = frameOffsetFixed +
                                faceLocalOffset + pos3DtoPos2DIso(micro);
                            writeTap(pixel, dist, col, shape.entityId, face);
                        }
                    }
                } else {
                    int dist = pos3DtoDistance(vp);
                    ivec2 pixel = trixelCanvasOffsetZ1 +
                        ivec2(floor(frameCanvasOffset)) +
                        faceLocalOffset + pos3DtoPos2DIso(vp);
                    writeTap(pixel, dist, col, shape.entityId, face);
                }
            }
        }
    }
}
