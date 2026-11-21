// Mirrors shaders/ir_canvas_image_command.glsl; keep the two in lockstep.
#ifndef IR_CANVAS_IMAGE_COMMAND_METAL_INCLUDED
#define IR_CANVAS_IMAGE_COMMAND_METAL_INCLUDED

// One image draw, in texels of its target: canvas texels for
// c_canvas_image_to_trixel, main framebuffer pixels for the
// image_to_framebuffer stages. Twin of IRRender::CanvasImageCommand
// (ir_render_types.hpp).
struct CanvasImageCommand {
    int2 clipLo;
    int2 clipHi;
    int2 origin;
    int2 size;
    float4 uvRect;         // u0, v0, u1, v1
    int2 baseSize;         // level 0 of the atlas
    int levelCount;
    int cell;
    uint tintPacked;
    int posterizeLevels;   // 0 = off
    int distance;
    int pad;
    float4 unrotate;       // row-major 2x2: target offset from the box centre -> unturned box
};

#endif // IR_CANVAS_IMAGE_COMMAND_METAL_INCLUDED
