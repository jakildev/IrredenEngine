#ifndef DEPTH_PROBE_H
#define DEPTH_PROBE_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_trixel_framebuffer.hpp>

// GPU→CPU composite-depth probe (#1910). A debug-only readback that, for a
// requested screen pixel, reads the REAL depth-test value stored in the main
// framebuffer's depth attachment and decodes it to shared trixel-distance
// units. Because every render path — the cardinal gather
// (f_trixel_to_framebuffer), the per-axis forward scatter (f_per_axis_scatter),
// and the detached-canvas composite (ENTITY_CANVAS_TO_FRAMEBUFFER) — writes its
// winning fragment's `gl_FragDepth` into this one attachment under the GL_LESS
// depth test, a single readback captures the true composite winner regardless of
// which path produced the pixel. The detached composite writes depth on BOTH
// backends — #1957 verified the depth-write state is default-ENABLED when the
// composite runs (Metal `depthWriteEnabled_`, OpenGL `glDepthMask(GL_TRUE)`) and
// made that write explicit so it can't silently regress — so the probe reads the
// detached solid's stored depth on both backends, not just the floor/world
// behind it. (The earlier "Metal composite is depth-blind — #1884/#1950 Finding
// 1" reading was a misdiagnosis: the composite participates in depth; where its
// iso-depth ranks behind the floor it LOSES the test, the #1958 wrong-winner
// problem, rather than never writing.) That is the reconciled, comparable depth
// the #1884 "behind-face wins" crossings need diagnosed in real units, and the
// reason this reads the attachment rather than approximating depth as a shader
// color output (which loses precision and can't be decoded per-path).
//
// `assertCompositeWritesDepth` is the #1957 regression guard: it turns one
// readback into a machine-readable PASS/FAIL so a future pass that disables the
// composite depth-write is caught headlessly (canvas_stress --depth-probe-assert).
//
// Pure readback: it touches no shader and no render state, so a scene with the
// probe off — or even on — is byte-identical at the pixel level. The cost is a
// full GPU flush per probed frame (device()->finish(): Metal commit+wait per
// #1436, GL glFinish), so keep it strictly debug-gated and single-pixel.
//
// Lives in the prefab layer (not IRRender::) because it resolves the
// "mainFramebuffer" prefab entity — engine/render/ owns graphics primitives, not
// scene entities. The generic depth-texture readback primitive it builds on is
// `Texture2D::getSubImage2D(..., PixelDataFormat::DEPTH_COMPONENT, FLOAT32, ...)`.
namespace IRPrefab::DepthProbe {

/// Result of a single-pixel composite-depth readback.
/// @c normDepth_ is the raw window depth in [0, 1] read from the attachment (the
/// depth-test winner across every render path). @c rawDist_ decodes it to the
/// shared trixel-distance units via the exact inverse of
/// @c f_trixel_to_framebuffer's @c normalizeDistance, using the same
/// @c kTrixelDistance{Min,Max}Distance constants — so the same world point reads
/// the same value regardless of which path produced it.
struct CompositeDepthSample {
    IRMath::ivec2 pixel_{-1, -1};
    float normDepth_ = 1.0f;
    float rawDist_ = 0.0f;
    bool valid_ = false;
};

/// Read and decode the main framebuffer's composite depth at pixel @p px.
/// Coordinates are in the MAIN FRAMEBUFFER's texture space (its
/// resolution-plus-buffer, logged at canvas creation, e.g. 642x722), top-left
/// origin — NOT window/screenshot pixels (FRAMEBUFFER_TO_SCREEN blits this
/// texture to the window under camera pan/zoom, so the screenshot is a
/// camera-dependent view of it). Call AFTER the framebuffer composite is
/// complete (after @c ENTITY_CANVAS_TO_FRAMEBUFFER / @c FRAMEBUFFER_TO_SCREEN).
/// Returns @c valid_ == false when @p px is outside the framebuffer.
inline CompositeDepthSample readbackCompositeDepth(IRMath::ivec2 px) {
    CompositeDepthSample sample;
    sample.pixel_ = px;

    const auto &framebuffer =
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const IRMath::ivec2 resolution = framebuffer.getResolutionPlusBuffer();
    if (px.x < 0 || px.y < 0 || px.x >= resolution.x || px.y >= resolution.y) {
        return sample;
    }

    // The offscreen framebuffer depth texture is bottom-left origin on OpenGL,
    // top-left on Metal; map the top-left screen pixel to the texel accordingly
    // (mirrors readDefaultFramebuffer's GL row flip so a pixel picked from an
    // auto-screenshot maps to the same texel on both backends).
    const int texelY = IRPlatform::kIsOpenGL ? (resolution.y - 1 - px.y) : px.y;

    // Flush so the readback sees this frame's committed composite. On Metal this
    // is the mandatory commit+wait before getBytes (#1436); on OpenGL glFinish.
    IRRender::device()->finish();

    // gl_FragDepth is written directly as window depth in [0, 1] on both
    // backends (the shader stores `normalizeDistance(...)`), so the value read
    // back IS the normalized distance — no NDC depth-range conversion needed.
    float windowDepth = 1.0f;
    framebuffer.getTextureDepth().getSubImage2D(
        px.x,
        texelY,
        1,
        1,
        IRRender::PixelDataFormat::DEPTH_COMPONENT,
        IRRender::PixelDataType::FLOAT32,
        &windowDepth
    );

    sample.normDepth_ = windowDepth;
    sample.rawDist_ = windowDepth * static_cast<float>(
                                        IRConstants::kTrixelDistanceMaxDistance -
                                        IRConstants::kTrixelDistanceMinDistance
                                    ) +
                      static_cast<float>(IRConstants::kTrixelDistanceMinDistance);
    sample.valid_ = true;
    return sample;
}

/// Convenience wrapper: read @p px and emit one machine-readable
/// @c IR_LOG_INFO line a human or script can read the depth ordering directly
/// from. Format: @c [depth-probe] pixel=(x,y) normDepth=… rawDist=…
inline void logCompositeDepth(IRMath::ivec2 px) {
    const CompositeDepthSample sample = readbackCompositeDepth(px);
    if (!sample.valid_) {
        IR_LOG_INFO("[depth-probe] pixel=({},{}) out-of-range (no framebuffer pixel)", px.x, px.y);
        return;
    }
    IR_LOG_INFO(
        "[depth-probe] pixel=({},{}) normDepth={:.6f} rawDist={:.1f}",
        sample.pixel_.x,
        sample.pixel_.y,
        sample.normDepth_,
        sample.rawDist_
    );
}

/// #1957 depth-write regression guard. Reads @p px (expected to fall inside a
/// world-placed detached solid) and emits one machine-readable verdict line:
/// @c [depth-probe-assert] pixel=(x,y) normDepth=… rawDist=… result=PASS|FAIL.
/// PASS means the composite stored a non-background depth there — i.e. the
/// detached-canvas composite WROTE the depth attachment. FAIL means the texel
/// reads the far-plane background (@c normDepth_ ~= 1.0): either the composite
/// stopped writing depth (the regression this guards) or no solid covers @p px.
/// A failed readback (pixel out of range) also reports FAIL so a mis-aimed probe
/// can't masquerade as a pass. Backend-agnostic: it reads the same shared depth
/// attachment on GL and Metal. Pure readback, so registering this guard never
/// perturbs the frame (byte-identical, like the diagnostic probe).
inline bool assertCompositeWritesDepth(IRMath::ivec2 px) {
    // Background is the depth clear at the far plane: rawDist == +kTrixelDistance
    // MaxDistance, i.e. normDepth == 1.0. Any composite-written surface reads
    // well below that (the canvas_stress canary reads ~0.49). 0.99 separates the
    // two with wide margin and no dependence on the exact stored distance.
    constexpr float kBackgroundNormDepthThreshold = 0.99f;
    const CompositeDepthSample sample = readbackCompositeDepth(px);
    const bool wroteDepth = sample.valid_ && sample.normDepth_ < kBackgroundNormDepthThreshold;
    IR_LOG_INFO(
        "[depth-probe-assert] pixel=({},{}) normDepth={:.6f} rawDist={:.1f} result={}",
        px.x,
        px.y,
        sample.normDepth_,
        sample.rawDist_,
        wroteDepth ? "PASS" : "FAIL"
    );
    return wroteDepth;
}

} // namespace IRPrefab::DepthProbe

#endif /* DEPTH_PROBE_H */
