#include <metal_stdlib>
using namespace metal;

#define IR_PER_AXIS_SURFACE_SHADOW 1
#define IR_PER_AXIS_FRAGMENT_NAME f_peraxis_surface_shadow
#include "ir_scatter_depth.metal"
#include "ir_per_axis_surface.metal"
#include "ir_sun_shadow_sample.metal"
#include "ir_peraxis_scatter_interface.metal"
#include "ir_peraxis_scatter_fragment_body.metal"
