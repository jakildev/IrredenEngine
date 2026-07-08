#ifndef DEPTH_PROBE_H
#define DEPTH_PROBE_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <cstdio>
#include <string>

// GPU→CPU composite-depth probe. A debug-only readback that, for a requested
// screen pixel, reads the REAL depth-test value stored in the main framebuffer's
// depth attachment and decodes it to shared trixel-distance units. Because every
// render path — the cardinal gather (f_trixel_to_framebuffer), the per-axis
// forward scatter (f_per_axis_scatter), and the detached-canvas composite
// (ENTITY_CANVAS_TO_FRAMEBUFFER) — writes its winning fragment's `gl_FragDepth`
// into this one attachment under the GL_LESS depth test, a single readback
// captures the true composite winner regardless of which path produced the
// pixel. The detached composite writes depth on BOTH backends (Metal
// `depthWriteEnabled_`, OpenGL `glDepthMask(GL_TRUE)`), made explicit so it can't
// silently regress — so the probe reads the detached solid's stored depth, not
// just the floor/world behind it. Reading the attachment (rather than
// approximating depth as a shader color output) keeps full precision and lets
// each path decode its own range.
//
// `assertCompositeWritesDepth` turns one readback into a machine-readable
// PASS/FAIL so a future pass that disables the composite depth-write is caught
// headlessly (canvas_stress --depth-probe-assert).
//
// Pure readback: it touches no shader and no render state, so a scene with the
// probe off — or even on — is byte-identical at the pixel level. The cost is a
// full GPU flush per probed frame (device()->finish(): Metal commit+wait, GL
// glFinish), so keep it strictly debug-gated and single-pixel.
//
// Lives in the prefab layer (not IRRender::) because it resolves the
// "mainFramebuffer" prefab entity — engine/render/ owns graphics primitives, not
// scene entities. The generic depth-texture readback primitive it builds on is
// `Texture2D::getSubImage2D(..., PixelDataFormat::DEPTH_COMPONENT, FLOAT32, ...)`.
// See #1910 (design), #1957 (depth-write guard).
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

namespace detail {

/// The @c [depth-probe] integer-encode decode, factored so the diagnostic probe
/// and the tier-assert guard partition @c enc the same way (one source of truth
/// for the #1960 N-tier layout). @c enc > kDepthForegroundCeil is WORLD content
/// (tier 0), encoded as @c iso*8 + flip*4 + face (#2207 riser-polarity carrier
/// at bit 2); @c enc inside the reserved band is a FOREGROUND fragment whose
/// disjoint sub-range names its priority tier (1..N-1). iso/face is the
/// @c encodeDepthWithFace inverse, taken tier-center-relative for a foreground
/// fragment so @c iso reads as the unit's local model-frame iso depth.
struct DecodedComposite {
    int enc_ = 0;
    int tier_ = 0;
    int iso_ = 0;
    int face_ = 0;
    int flip_ = 0;
};

inline DecodedComposite decodeComposite(const CompositeDepthSample &sample) {
    DecodedComposite decoded;
    decoded.enc_ = static_cast<int>(IRMath::roundHalfUp(sample.rawDist_));
    if (decoded.enc_ <= IRRender::kDepthForegroundCeil) {
        const int slot = (decoded.enc_ - IRConstants::kTrixelDistanceMinDistance) /
                         IRRender::kDepthForegroundTierWidth;
        decoded.tier_ = IRMath::clamp(
            IRRender::kDepthForegroundTierCount - 1 - slot,
            1,
            IRRender::kDepthForegroundTierCount - 1
        );
    }
    const int encRel = decoded.tier_ == 0
                           ? decoded.enc_
                           : decoded.enc_ - IRRender::depthForegroundTierCenter(decoded.tier_);
    const int shift = IRRender::kDepthEncodeShift;
    const int lowBits = ((encRel % shift) + shift) % shift;
    decoded.face_ = lowBits & 3;
    decoded.flip_ = lowBits >> 2;
    decoded.iso_ = (encRel - lowBits) / shift;
    return decoded;
}

} // namespace detail

/// Convenience wrapper: read @p px and emit one machine-readable
/// @c IR_LOG_INFO line a human or script can read the depth ordering directly
/// from. Format: @c [depth-probe] pixel=(x,y) normDepth=… rawDist=…
inline void logCompositeDepth(IRMath::ivec2 px) {
    const CompositeDepthSample sample = readbackCompositeDepth(px);
    if (!sample.valid_) {
        IR_LOG_INFO("[depth-probe] pixel=({},{}) out-of-range (no framebuffer pixel)", px.x, px.y);
        return;
    }
    // Decode the partitioned enc so a debugger reads the depth ordering directly;
    // without it the partitioned enc reads as an opaque rawDist (the Finding-1-style
    // mis-read the plan flagged). Reporting the numeric tier makes the per-trixel
    // `tier = max(entity, trixel)` headlessly ground-truthable at an interpenetration
    // pixel (#1960 acceptance G).
    const detail::DecodedComposite decoded = detail::decodeComposite(sample);
    IR_LOG_INFO(
        "[depth-probe] pixel=({},{}) normDepth={:.6f} rawDist={:.1f} enc={} tier={} ({}) iso={} "
        "face={}",
        sample.pixel_.x,
        sample.pixel_.y,
        sample.normDepth_,
        sample.rawDist_,
        decoded.enc_,
        decoded.tier_,
        decoded.tier_ == 0 ? "world" : "foreground",
        decoded.iso_,
        decoded.face_
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

/// #1960 per-trixel-priority tier regression guard. Reads @p px (expected to fall
/// on an interpenetration overlap where a priority-tagged solid must win) and
/// emits one machine-readable verdict line:
/// @c [depth-probe-assert] pixel=(x,y) normDepth=… rawDist=… enc=… tier=N
/// expected=M result=PASS|FAIL. PASS iff the composite winner at @p px decodes to
/// the @p expectedTier (the #1960 N-tier partition; @c detail::decodeComposite).
/// This is the positive ENABLED-path guard the per-trixel carrier needs (a
/// default-off feature that byte-identity at default cannot prove works) — it
/// catches a future pass that drops the carrier so the priority solid falls back
/// to tier 0 and loses the depth contest. A failed readback (pixel out of range)
/// decodes to tier 0 with @c valid_ == false, so an out-of-range probe reports
/// FAIL for any non-default @p expectedTier rather than masquerading as a pass.
/// Backend-agnostic, pure readback (byte-identical), like @c assertCompositeWritesDepth.
inline bool assertCompositeDepthTier(IRMath::ivec2 px, int expectedTier) {
    const CompositeDepthSample sample = readbackCompositeDepth(px);
    const detail::DecodedComposite decoded = detail::decodeComposite(sample);
    const bool match = sample.valid_ && decoded.tier_ == expectedTier;
    IR_LOG_INFO(
        "[depth-probe-assert] pixel=({},{}) normDepth={:.6f} rawDist={:.1f} enc={} tier={} "
        "expected={} result={}",
        px.x,
        px.y,
        sample.normDepth_,
        sample.rawDist_,
        decoded.enc_,
        decoded.tier_,
        expectedTier,
        match ? "PASS" : "FAIL"
    );
    return match;
}

/// Parse a `--depth-probe`-style `"X,Y"` framebuffer-pixel flag value into
/// @p pixelOut. Returns true and writes @p pixelOut on a well-formed pair; on a
/// malformed value returns false, logs one warn line naming @p flag, and leaves
/// @p pixelOut untouched — so a caller gates its probe off the return value and a
/// bad value keeps the run byte-identical rather than probing a garbage pixel.
/// Shared by the demos' `--depth-probe` handling (canvas_stress, perf_grid):
/// IRArgs has no pair type, so the comma split lives demo-side against the string
/// value. (The `--depth-probe-assert X,Y,tier=N` superset parse stays in
/// canvas_stress — this covers only the plain X,Y form the two demos share.)
inline bool parsePixelArg(const std::string &value, IRMath::ivec2 &pixelOut, const char *flag) {
    int px = 0;
    int py = 0;
    if (std::sscanf(value.c_str(), "%d,%d", &px, &py) == 2) {
        pixelOut = IRMath::ivec2(px, py);
        return true;
    }
    IR_LOG_WARN("{}: expected X,Y (e.g. 960,540); ignoring '{}'", flag, value);
    return false;
}

} // namespace IRPrefab::DepthProbe

#endif /* DEPTH_PROBE_H */
