#version 450 core

#define IR_PER_AXIS_SURFACE_LIGHTING 1
#include "ir_scatter_depth.glsl"
#include "ir_per_axis_surface.glsl"
#include "ir_sun_face_query_layout.glsl"
#include "ir_sun_shadow_sample.glsl"
#include "ir_world_surface_lighting.glsl"

layout(binding = 4) uniform sampler2D surfaceAO;

#define IR_PER_AXIS_SURFACE_FOG 1
#define IR_FOG_OBSERVER_BINDING 10
#define IR_FOG_GRID_SAMPLER_BINDING 8
#define IR_FOG_LOS_SAMPLER_BINDING 9
#include "ir_fog_common.glsl"

#include "ir_peraxis_scatter_fragment_body.glsl"
