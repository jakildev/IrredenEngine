#ifndef IRREDEN_GUI_TEST_ASSERTIONS_H
#define IRREDEN_GUI_TEST_ASSERTIONS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/render/widgets.hpp>

#include <optional>
#include <string>
#include <unordered_set>

// Phase 3 of the GUI + mouse verification harness (#1796). Capture-frame
// assertions over the introspectable widget + picking state, driven by the
// #1795 scripted-shot harness in engine/video. Evaluation lives here (the
// prefab layer) rather than in engine/video because it needs widget / picking
// components engine/video cannot see — the harness calls in through the
// type-erased GuiTestConfig::onAssertFrame_ function pointer.
//
// Usage (a creation, after its widgets exist): build a per-shot
// `Assertion` table, own one `LatchState`, and forward the harness's
// onAssertFrame_ callback to `onFrame`. Each evaluated assertion emits one
// machine-readable `GUI-ASSERT ...` log line the P4 gui-verify skill greps
// the way render-verify parses image diffs.
namespace IRPrefab::GuiTest {

enum class AssertKind {
    HOVERS,           // IRPrefab::Widget::hoveredWidget() == widget_
    CLICK_FIRES,      // widget_ pulsed C_WidgetState::fireAction_ during the shot
    SLIDER_VALUE,     // |sliderValue(widget_) - expectedFloat_| <= tolerance_
    CHECKBOX,         // checkboxState(widget_) == expectedBool_
    PICKS_VOXEL,      // castVoxelRay() hits with voxelPos_ == expectedVoxel_
    PICKS_ISO_COLUMN, // castVoxelRay() hits a voxel on expectedVoxel_'s iso column
};

// One assertion evaluated at a shot's capture frame. Only the fields a given
// kind reads are meaningful; the rest stay default.
struct Assertion {
    AssertKind kind_ = AssertKind::HOVERS;
    IREntity::EntityId widget_ = IREntity::kNullEntity; // widget-target kinds
    float expectedFloat_ = 0.0f;                        // SLIDER_VALUE
    float tolerance_ = 0.001f;                          // SLIDER_VALUE
    bool expectedBool_ = false;                         // CHECKBOX
    IRMath::ivec3 expectedVoxel_ = IRMath::ivec3(0);    // PICKS_VOXEL
    const char *label_ = "assert";                      // human-readable tag
};

// Factory helpers — readable, order-safe assertion construction at call sites
// (a creation builds its per-shot tables from these).
inline Assertion hovers(IREntity::EntityId widget, const char *label = "hovers") {
    Assertion assertion;
    assertion.kind_ = AssertKind::HOVERS;
    assertion.widget_ = widget;
    assertion.label_ = label;
    return assertion;
}

inline Assertion clickFires(IREntity::EntityId widget, const char *label = "click_fires") {
    Assertion assertion;
    assertion.kind_ = AssertKind::CLICK_FIRES;
    assertion.widget_ = widget;
    assertion.label_ = label;
    return assertion;
}

inline Assertion sliderValue(
    IREntity::EntityId widget,
    float expected,
    float tolerance = 0.001f,
    const char *label = "slider_value"
) {
    Assertion assertion;
    assertion.kind_ = AssertKind::SLIDER_VALUE;
    assertion.widget_ = widget;
    assertion.expectedFloat_ = expected;
    assertion.tolerance_ = tolerance;
    assertion.label_ = label;
    return assertion;
}

inline Assertion
checkbox(IREntity::EntityId widget, bool expected, const char *label = "checkbox") {
    Assertion assertion;
    assertion.kind_ = AssertKind::CHECKBOX;
    assertion.widget_ = widget;
    assertion.expectedBool_ = expected;
    assertion.label_ = label;
    return assertion;
}

inline Assertion picksVoxel(IRMath::ivec3 expected, const char *label = "picks_voxel") {
    Assertion assertion;
    assertion.kind_ = AssertKind::PICKS_VOXEL;
    assertion.expectedVoxel_ = expected;
    assertion.label_ = label;
    return assertion;
}

// Assert the click's ray hit a voxel on @p targetVoxel's iso column (same
// screen projection) — the occlusion-independent test of screen→world mapping
// accuracy. Unlike picksVoxel it does not require the target to be the
// front-most voxel, so it validates the mapping even when scene geometry sits
// in front of the aimed cell.
inline Assertion picksIsoColumn(IRMath::ivec3 targetVoxel, const char *label = "picks_iso_column") {
    Assertion assertion;
    assertion.kind_ = AssertKind::PICKS_ISO_COLUMN;
    assertion.expectedVoxel_ = targetVoxel;
    assertion.label_ = label;
    return assertion;
}

// Caller-owned latch. CLICK_FIRES needs it: C_WidgetState::fireAction_ is a
// single-frame pulse on click-release, gone by the post-settle capture frame,
// so we accumulate which widgets fired across the shot window. The creation
// owns one instance and threads it through onFrame — system/harness-owned
// state must never be a function-local static (.claude/rules/cpp-systems.md).
struct LatchState {
    std::unordered_set<IREntity::EntityId> firedWidgets_;
};

namespace detail {

inline const char *kindName(AssertKind kind) {
    switch (kind) {
    case AssertKind::HOVERS:
        return "HOVERS";
    case AssertKind::CLICK_FIRES:
        return "CLICK_FIRES";
    case AssertKind::SLIDER_VALUE:
        return "SLIDER_VALUE";
    case AssertKind::CHECKBOX:
        return "CHECKBOX";
    case AssertKind::PICKS_VOXEL:
        return "PICKS_VOXEL";
    case AssertKind::PICKS_ISO_COLUMN:
        return "PICKS_ISO_COLUMN";
    }
    return "UNKNOWN";
}

// Record every widget whose fireAction_ pulsed this frame.
inline void latchFires(LatchState &latch) {
    IREntity::forEachComponent<IRComponents::C_WidgetState>(
        [&latch](IREntity::EntityId &id, IRComponents::C_WidgetState &state) {
            if (!state.fireAction_)
                return;
            latch.firedWidgets_.insert(id);
        }
    );
}

inline bool firedThisShot(const LatchState &latch, IREntity::EntityId widget) {
    return latch.firedWidgets_.count(widget) != 0;
}

} // namespace detail

// Evaluate one assertion at the capture frame. Returns true on pass and writes
// a short description of the observed value into `actual` for the log line.
inline bool evaluateOne(const Assertion &assertion, const LatchState &latch, std::string &actual) {
    switch (assertion.kind_) {
    case AssertKind::HOVERS: {
        const IREntity::EntityId hovered = IRPrefab::Widget::hoveredWidget();
        actual = "hovered=" + std::to_string(hovered);
        return hovered == assertion.widget_;
    }
    case AssertKind::CLICK_FIRES: {
        const bool fired = detail::firedThisShot(latch, assertion.widget_);
        actual = fired ? "fired" : "no-fire";
        return fired;
    }
    case AssertKind::SLIDER_VALUE: {
        const float value = IRPrefab::Widget::sliderValue(assertion.widget_);
        actual = "value=" + std::to_string(value);
        return IRMath::abs(value - assertion.expectedFloat_) <= assertion.tolerance_;
    }
    case AssertKind::CHECKBOX: {
        const bool checked = IRPrefab::Widget::checkboxState(assertion.widget_);
        actual = checked ? "checked" : "unchecked";
        return checked == assertion.expectedBool_;
    }
    case AssertKind::PICKS_VOXEL: {
        const std::optional<IRPrefab::Picking::RayHit> hit = IRPrefab::Picking::castVoxelRay();
        if (!hit) {
            actual = "miss";
            return false;
        }
        const IRMath::ivec3 v = hit->voxelPos_;
        actual = "voxel=(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," +
                 std::to_string(v.z) + ")";
        return v == assertion.expectedVoxel_;
    }
    case AssertKind::PICKS_ISO_COLUMN: {
        const std::optional<IRPrefab::Picking::RayHit> hit = IRPrefab::Picking::castVoxelRay();
        if (!hit) {
            actual = "miss";
            return false;
        }
        // Screen→world mapping accuracy is a 2D iso-column property: the click
        // must land the ray on the target voxel's iso column. Which voxel along
        // that column is front-most is scene/occlusion-dependent, not a mapping
        // fact, so compare iso projections rather than exact world voxels.
        const IRMath::ivec2 hitIso = IRMath::pos3DtoPos2DIso(hit->voxelPos_);
        const IRMath::ivec2 wantIso = IRMath::pos3DtoPos2DIso(assertion.expectedVoxel_);
        actual = "iso=(" + std::to_string(hitIso.x) + "," + std::to_string(hitIso.y) + ") want=(" +
                 std::to_string(wantIso.x) + "," + std::to_string(wantIso.y) + ")";
        return hitIso == wantIso;
    }
    }
    actual = "unknown-kind";
    return false;
}

// Evaluate every assertion for a shot, emitting one machine-readable line each:
//   GUI-ASSERT shot=<i> label=<shot> kind=<KIND> target=<eid> name=<tag>
//              result=PASS|FAIL actual=<observed>
inline void evaluate(
    const LatchState &latch,
    int shotIndex,
    const char *shotLabel,
    const Assertion *assertions,
    int count
) {
    for (int i = 0; i < count; ++i) {
        const Assertion &assertion = assertions[i];
        std::string actual;
        const bool pass = evaluateOne(assertion, latch, actual);
        IR_LOG_INFO(
            "GUI-ASSERT shot={} label={} kind={} target={} name={} result={} actual={}",
            shotIndex,
            shotLabel != nullptr ? shotLabel : "",
            detail::kindName(assertion.kind_),
            assertion.widget_,
            assertion.label_ != nullptr ? assertion.label_ : "",
            pass ? "PASS" : "FAIL",
            actual
        );
    }
}

// Per-frame driver wired to GuiTestConfig::onAssertFrame_. Latches one-frame
// pulses every live frame; on the capture frame, evaluates + logs all of the
// shot's assertions, then clears the latch so the next shot starts clean.
inline void onFrame(
    LatchState &latch,
    int shotIndex,
    bool isCaptureFrame,
    const char *shotLabel,
    const Assertion *assertions,
    int count
) {
    detail::latchFires(latch);
    if (!isCaptureFrame)
        return;
    evaluate(latch, shotIndex, shotLabel, assertions, count);
    latch.firedWidgets_.clear();
}

} // namespace IRPrefab::GuiTest

#endif /* IRREDEN_GUI_TEST_ASSERTIONS_H */
