// Shared cardinal-layout micro-cell emit for the sun-shadow RESOLVE scatter
// kernels (c_resolve_per_axis_screen_depth, c_resolve_world_placed_depth).
// Both kernels re-project a foreign R32I distance canvas into the SCREEN-SPACE
// scratch buffer laid out exactly like the main canvas distance texture. Their
// decodes differ, but the emit that ends each — encode the micro-cell's depth
// key, project it to its iso pixel, atomicMin it into the region's two-pixel
// diamond — is the same math. One definition keeps a one-sided edit from
// desyncing the two resolve routes' footprints against the BAKE recovery that
// reads them (the ir_voxel_face_select.glsl idiom).
//
// This is an include-FRAGMENT, not a standalone shader: the kernel wrappers
// include it AFTER ir_iso_common.glsl (prerequisite helpers: pos3DtoDistance,
// pos3DtoPos2DIso, encodeDepthWithFace, faceOffset_2x3, isInsideCanvas) and
// AFTER their own `resolveScratch` SSBO declaration, which this function
// writes through. GLSL has no way to pass an SSBO as an argument, so the
// scratch binding is a wrapper-supplied contract rather than a parameter —
// the same reason the fog grid's image slot is a wrapper #define in
// ir_voxel_face_select.glsl. The fragment includes nothing itself: the GLSL
// include resolver is non-recursive (#2514), so every prerequisite must be
// pulled in by the wrapper ahead of this file.
// Metal twin: metal/ir_resolve_cardinal_emit.metal — keep byte-identical math.

// Emit one micro-cell's two-pixel diamond region (#1724) into the resolve
// scratch, in the MAIN-canvas cardinal distance layout.
//
// `viewPos` is the micro-cell already rotated into the cardinal VIEW frame and
// expressed in subdivision units; `slot`/`flip` are the stored key bits, which
// ride the encode so polarity survives the resolve bridge (#2207). Emitting the
// two-pixel region rather than the single origin pixel is load-bearing:
// roundHalfUp collapses a region's input pixels onto one recovered cell, so a
// single-pixel write left the resolve texture ~50% sparse — pinhole casters
// whose shadows dithered with interior gaps.
//
// `regionAxis` is the VIEW-frame face axis that picks the diamond region, and
// is NOT always derivable from `slot`: the per-axis store is already in the
// view frame (visibleFaceTripletCardinal orders the triplet so slot s lands on
// view axis s), while the world-placed store is model-frame and must rotate
// its face normal into the view frame first. Passing it in keeps both callers
// on this one emit. faceOffset_2x3 is polarity-blind (axis only), so the flip
// never reaches the region choice.
void emitResolveCardinalDiamond(
    const ivec3 viewPos,
    const int regionAxis,
    const int slot,
    const int flip,
    const ivec2 mainBase,
    const ivec2 canvasSize
) {
    // Per-micro-cell depth, shared by the region's two pixels — the exact
    // encode a real cardinal store would hold here, so BAKE's pixel+depth
    // inverse recovers points on the face plane.
    const int encoded = encodeDepthWithFace(pos3DtoDistance(viewPos), slot, flip);
    const ivec2 cellBase = mainBase + pos3DtoPos2DIso(viewPos);
    for (int k = 0; k < 2; ++k) {
        const ivec2 mainPixel = cellBase + faceOffset_2x3(regionAxis, k);
        if (!isInsideCanvas(mainPixel, canvasSize)) {
            continue;
        }
        // Front-most per screen pixel: smallest encoded distance wins (depth
        // dominates the low bits — flip+slot), exactly like the main canvas
        // atomicMin store.
        atomicMin(resolveScratch[mainPixel.y * canvasSize.x + mainPixel.x], encoded);
    }
}
