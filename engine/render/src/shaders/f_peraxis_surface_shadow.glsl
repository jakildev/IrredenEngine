#version 450 core

#define IR_PER_AXIS_SURFACE_SHADOW 1
#include "ir_scatter_depth.glsl"
#include "ir_per_axis_surface.glsl"
#include "ir_sun_shadow_sample.glsl"
#include "ir_peraxis_scatter_fragment_body.glsl"
