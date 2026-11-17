#version 450 core
#include "ir_post_effects_common.glsl"
#define IR_POST_EFFECTS_BLUR_AXIS ivec2(0, 1)
#include "ir_post_effects_blur_body.glsl"
