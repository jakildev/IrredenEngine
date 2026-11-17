#include "ir_canvas_image_sample.metal"

// One image draw onto a trixel canvas. Mirrors
// shaders/c_canvas_image_to_trixel.glsl.
//
// Workgroup dimensions: (8, 8, 1), one thread per canvas texel of the draw's
// clipped box, dispatched from `clipLo`.

kernel void c_canvas_image_to_trixel(
    device const CanvasImageCommand* commands [[buffer(12)]],
    texture2d<float, access::write> canvasColors [[texture(0)]],
    texture2d<int, access::write> canvasDistances [[texture(1)]],
    texture2d<float, access::read> imageAtlas [[texture(2)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const CanvasImageCommand cmd = commands[0];
    const int2 pixel = cmd.clipLo + int2(globalId.xy);
    const int2 canvasSize = int2(
        int(canvasColors.get_width()),
        int(canvasColors.get_height())
    );
    if (pixel.x >= cmd.clipHi.x || pixel.y >= cmd.clipHi.y ||
        !isInsideCanvas(pixel, canvasSize)) {
        return;
    }

    float3 rgb;
    if (!canvasImageColor(imageAtlas, cmd, pixel, rgb)) {
        return;
    }

    canvasColors.write(float4(rgb, 1.0), uint2(pixel));
    canvasDistances.write(int4(cmd.distance, 0, 0, 0), uint2(pixel));
}
