#version 450 core
#include "ir_pick_record.glsl"
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(std140, binding = 3) uniform PickImageFrame {
    vec4 prefix[18];
    ivec2 presentedPickSize;
};
layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    PickRecord picks[];
};
void main() {
    const uint index = (gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x) * 64u + gl_LocalInvocationID.x;
    if (index > uint(presentedPickSize.x * presentedPickSize.y)) return;
    picks[index] = PickRecord(uvec2(0), 1.0, 0.0, uvec2(0), 1.0, 0u);
}
