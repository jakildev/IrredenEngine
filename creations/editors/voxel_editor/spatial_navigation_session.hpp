#ifndef IR_VOXEL_EDITOR_SPATIAL_NAVIGATION_SESSION_H
#define IR_VOXEL_EDITOR_SPATIAL_NAVIGATION_SESSION_H

#include "palette.hpp"
#include "session_builder.hpp"

#include <irreden/render/camera.hpp>

#include <array>

namespace IRVoxelEditor::Session {

enum class SpatialNavigationOperation {
    ORBIT,
    PINCH,
    WHEEL,
    UI_ORBIT,
    UI_PINCH,
    UI_START,
    UI_CROSSING,
    CURSOR_DRAG,
    ROLL_DRAG,
    PRECISE_PAN,
    SPACE_PAN,
    RESET_KEY,
    RESET_BUTTON,
    MODIFIER_RELEASE,
    UI_WHEEL,
};

struct SpatialNavigationCase {
    const char *label_;
    SpatialNavigationOperation operation_;
    IRMath::OrbitAngles initial_;
    IRMath::OrbitAngles expected_;
    IRMath::vec2 delta_{};
    float initialZoom_ = 4.0f;
    float expectedZoom_ = 4.0f;
};

// Independent expected poses use the documented default input rates: .04
// radians per precise-scroll unit, .005 per drag point, .125 wheel octaves,
// and one octave per magnification unit. Tests must change if that UX changes.
inline const std::array<SpatialNavigationCase, 20> &spatialNavigationCases() {
    using Op = SpatialNavigationOperation;
    static const std::array<SpatialNavigationCase, 20> cases{{
        {"orbit_xy_30", Op::ORBIT, {.5235988f, .5f, 0}, {.6035988f, .46f, 0}, {2, -1}},
        {"orbit_underside_67",
         Op::ORBIT,
         {1.1693706f, -.4f, 0},
         {1.1193706f, -.34f, 0},
         {-1.25f, 1.5f}},
        {"orbit_across_horizon", Op::ORBIT, {.5235988f, .01f, 0}, {.5435988f, -.03f, 0}, {.5f, -1}},
        {"orbit_preserves_roll",
         Op::ORBIT,
         {1.1693706f, .7f, .3f},
         {1.1993706f, .68f, .3f},
         {.75f, -.5f}},
        {"pinch_cursor_anchor",
         Op::PINCH,
         {.5235988f, .5f, .2f},
         {.5235988f, .5f, .2f},
         {0, .25f},
         4,
         4.75682846f},
        {"wheel_cursor_anchor_underside",
         Op::WHEEL,
         {1.1693706f, -.4f, 0},
         {1.1693706f, -.4f, 0},
         {0, .75f},
         4,
         4.26856160f},
        {"panel_blocks_orbit", Op::UI_ORBIT, {.5235988f, .5f, 0}, {.5235988f, .5f, 0}, {2, -1}},
        {"panel_blocks_pinch",
         Op::UI_PINCH,
         {1.1693706f, -.4f, 0},
         {1.1693706f, -.4f, 0},
         {0, .25f}},
        {"panel_start_keeps_ownership",
         Op::UI_START,
         {.5235988f, .5f, 0},
         {.5235988f, .5f, 0},
         {2, -1}},
        {"viewport_start_keeps_ownership",
         Op::UI_CROSSING,
         {1.1693706f, -.4f, 0},
         {1.2493706f, -.44f, 0},
         {2, -1}},
        {"option_drag_cursor_anchor",
         Op::CURSOR_DRAG,
         {.5235988f, .5f, 0},
         {.8235988f, .35f, 0},
         {60, -30}},
        {"option_shift_drag_roll",
         Op::ROLL_DRAG,
         {1.1693706f, -.4f, .2f},
         {1.1693706f, -.4f, .5f},
         {60, 0}},
        {"shift_trackpad_pan",
         Op::PRECISE_PAN,
         {.5235988f, .5f, .2f},
         {.5235988f, .5f, .2f},
         {1.5f, -.75f}},
        {"space_drag_pan",
         Op::SPACE_PAN,
         {1.1693706f, -.4f, .2f},
         {1.1693706f, -.4f, .2f},
         {30, -18}},
        {"home_resets_view",
         Op::RESET_KEY,
         {1.1693706f, -.4f, .2f},
         {0, .6154797087f, 0},
         {},
         7.75f,
         4},
        {"button_resets_view",
         Op::RESET_BUTTON,
         {.5235988f, .5f, .2f},
         {0, .6154797087f, 0},
         {},
         7.75f,
         4},
        {"modifier_first_release_keeps_drag",
         Op::MODIFIER_RELEASE,
         {.5235988f, .5f, 0},
         {.9735988f, .45f, 0},
         {60, -30}},
        {"orbit_clamps_above_pole",
         Op::ORBIT,
         {.5235988f, 1.4f, 0},
         {.5635988f, IRMath::kHalfPi - .01f, 0},
         {1, 100}},
        {"panel_blocks_wheel",
         Op::UI_WHEEL,
         {1.1693706f, -.4f, 0},
         {1.1693706f, -.4f, 0},
         {0, .75f}},
        {"orbit_clamps_below_pole",
         Op::ORBIT,
         {1.1693706f, -1.4f, 0},
         {1.1293706f, -IRMath::kHalfPi + .01f, 0},
         {-1, -100}},
    }};
    return cases;
}

inline constexpr int kSpatialNavigationShotCount = 20;
inline IRMath::vec2 spatialNavigationAim() {
    return {800, 650};
}

// main.cpp owns the authored document and gesture state. Its preparation
// seeds a non-empty document, applies this case's initial pose and zoom,
// records its authored hash and anchor pixels, then leaves replay to the UI.
// Its witness checks unchanged authored data for every case, pivot continuity
// for orbit/zoom, nonzero pan without orbit, reset framing and UI ownership.
void prepareSpatialNavigation(int shot);
bool evaluateSpatialNavigationEditorWitness(int shot, std::string &actual);
IREntity::EntityId spatialNavigationEditableEntity();

// Samples actual NORMALS framebuffer pixels, independently of a projected
// fixture aim. Three-by-three face interiors avoid raster-edge coverage
// ambiguity. The CPU ray at each displayed pixel must enter the same signed
// world face; underside views therefore have to draw and pick positive Z.
inline bool evaluateSpatialNavigationSurface(const void *, std::string &actual) {
    const auto entity = spatialNavigationEditableEntity();
    const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
    if (set.globalPositions_.empty()) {
        actual = "no voxel positions";
        return false;
    }
    const auto framebuffer = IRRender::getViewport();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(framebuffer.x) * framebuffer.y * 4);
    if (!IRRender::readDefaultFramebuffer(0, 0, framebuffer.x, framebuffer.y, pixels.data())) {
        actual = "framebuffer read failed";
        return false;
    }
    const auto origin = set.globalPositions_[0].pos_;
    IRMath::vec2 low(framebuffer), high(0);
    IRMath::ivec3 occupiedLow(set.size_), occupiedHigh(0);
    for (int z = 0; z < set.size_.z; ++z)
        for (int y = 0; y < set.size_.y; ++y)
            for (int x = 0; x < set.size_.x; ++x) {
                const IRMath::ivec3 cell(x, y, z);
                if (set.voxels_[IRMath::index3DtoIndex1D(cell, set.size_)].color_.alpha_ == 0)
                    continue;
                occupiedLow = IRMath::min(occupiedLow, cell);
                occupiedHigh = IRMath::max(occupiedHigh, cell);
            }
    for (int z : {0, 1})
        for (int y : {0, 1})
            for (int x : {0, 1}) {
                const IRMath::vec3 corner(
                    x ? occupiedHigh.x + .5f : occupiedLow.x - .5f,
                    y ? occupiedHigh.y + .5f : occupiedLow.y - .5f,
                    z ? occupiedHigh.z + .5f : occupiedLow.z - .5f
                );
                const auto pixel = IRRender::windowPointsToFramebufferPx(
                    IRRender::worldPos3DToWindowPointsExact(origin + corner)
                );
                low = IRMath::min(low, pixel);
                high = IRMath::max(high, pixel);
            }
    const auto normalAt = [&](int x, int y) {
        const auto offset = (static_cast<std::size_t>(y) * framebuffer.x + x) * 4;
        IRMath::ivec3 normal(0);
        int nonzero = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const int channel = pixels[offset + axis];
            if (channel == 0 || channel == 255) {
                normal[axis] = channel == 0 ? -1 : 1;
                ++nonzero;
            } else if (channel != 128)
                return IRMath::ivec3(0);
        }
        return nonzero == 1 ? normal : IRMath::ivec3(0);
    };
    const auto direction = IRRender::unprojectView(IRMath::vec2(0), 3);
    int shown = 0, agree = 0;
    std::string mismatch;
    const auto rasterCorrection = IRRender::cameraRasterCorrectionFramebufferPx();
    std::array<int, 6> faces{};
    for (int y = IRMath::max(1, static_cast<int>(IRMath::floor(low.y)));
         y < IRMath::min(framebuffer.y - 1, static_cast<int>(IRMath::ceil(high.y)));
         y += 2) {
        for (int x = IRMath::max(1, static_cast<int>(IRMath::floor(low.x)));
             x < IRMath::min(framebuffer.x - 1, static_cast<int>(IRMath::ceil(high.x)));
             x += 2) {
            const auto normal = normalAt(x, y);
            if (normal == IRMath::ivec3(0))
                continue;
            bool interior = true;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    interior = interior && normalAt(x + dx, y + dy) == normal;
            if (!interior)
                continue;
            ++shown;
            const auto window =
                IRRender::framebufferPxToWindowPoints(IRMath::vec2(x + .5f, y + .5f));
            const auto displayedRay =
                IRRender::windowPointsToDisplayedWorldPos3DAtViewDepth(window, 0);
            const auto hit = IRPrefab::Picking::castGridRay(
                displayedRay - origin,
                direction,
                set.size_,
                [&](IRMath::ivec3 cell) {
                    return set.voxels_[IRMath::index3DtoIndex1D(cell, set.size_)].color_.alpha_ !=
                           0;
                }
            );
            if (!hit || hit->faceNormal_ != normal) {
                if (shown - agree <= 4) {
                    const auto picked = hit ? hit->faceNormal_ : IRMath::ivec3(0);
                    mismatch += " pixel=" + std::to_string(x) + "," + std::to_string(y) +
                                " draw=" + std::to_string(normal.x) + "," +
                                std::to_string(normal.y) + "," + std::to_string(normal.z) +
                                " pick=" + std::to_string(picked.x) + "," +
                                std::to_string(picked.y) + "," + std::to_string(picked.z);
                }
                continue;
            }
            ++agree;
            for (int axis = 0; axis < 3; ++axis)
                if (normal[axis])
                    ++faces[axis * 2 + (normal[axis] > 0 ? 1 : 0)];
        }
    }
    bool visibleFaces = true;
    for (int axis = 0; axis < 3; ++axis)
        if (IRMath::abs(direction[axis]) > .2f)
            visibleFaces = visibleFaces && faces[axis * 2 + (direction[axis] < 0 ? 1 : 0)] >= 4;
    actual = "normalPixels=" + std::to_string(shown) + " pickAgreements=" + std::to_string(agree) +
             " faces=" + std::to_string(faces[0]) + "," + std::to_string(faces[1]) + "," +
             std::to_string(faces[2]) + "," + std::to_string(faces[3]) + "," +
             std::to_string(faces[4]) + "," + std::to_string(faces[5]) +
             " rasterCorrection=" + std::to_string(rasterCorrection.x) + "," +
             std::to_string(rasterCorrection.y) + mismatch;
    return shown >= 12 && agree == shown && visibleFaces;
}

// Run after the ordinary GUI aim fixups: drag distances are logical window
// points, whereas panel/viewport locations are GUI trixels. Keeping those
// units separate makes the expected orbit independent of display DPI.
inline void resolveSpatialNavigationDragAims(Segment &segment, int shot) {
    using Op = SpatialNavigationOperation;
    const auto &spec = spatialNavigationCases()[shot];
    if (spec.operation_ != Op::CURSOR_DRAG && spec.operation_ != Op::ROLL_DRAG &&
        spec.operation_ != Op::MODIFIER_RELEASE && spec.operation_ != Op::SPACE_PAN)
        return;
    const auto origin = IRRender::guiTrixelToScreenPx(spatialNavigationAim());
    for (auto &event : segment.events_) {
        if (event.type_ != IRVideo::GuiInputEvent::Type::MOVE)
            continue;
        if (event.frameOffset_ == 6)
            event.screenPx_ = origin + IRMath::ivec2(spec.delta_);
        if (event.frameOffset_ == 10)
            event.screenPx_ = origin + IRMath::ivec2(spec.delta_) + IRMath::ivec2(30, 20);
    }
}

inline bool evaluateSpatialNavigation(const void *context, std::string &actual) {
    const int shot = *static_cast<const int *>(context);
    const auto &spec = spatialNavigationCases()[shot];
    const auto pose = IRPrefab::Camera::getOrbitAngles();
    const auto &expected = spec.expected_;
    const float zoom = IRRender::getCameraZoom().x;
    const bool poseMatches =
        IRMath::abs(IRMath::wrapAnglePi(pose.azimuth_ - expected.azimuth_)) < .0002f &&
        IRMath::abs(pose.elevation_ - expected.elevation_) < .0002f &&
        IRMath::abs(IRMath::wrapAnglePi(pose.roll_ - expected.roll_)) < .0002f;
    std::string editor;
    const bool editorMatches = evaluateSpatialNavigationEditorWitness(shot, editor);
    actual =
        "az=" + std::to_string(pose.azimuth_) + " elevation=" + std::to_string(pose.elevation_) +
        " roll=" + std::to_string(pose.roll_) + " zoom=" + std::to_string(zoom) + " expected=(" +
        std::to_string(expected.azimuth_) + "," + std::to_string(expected.elevation_) + "," +
        std::to_string(expected.roll_) + "," + std::to_string(spec.expectedZoom_) + ") " + editor;
    return IRRender::isSpatialViewEnabled() && poseMatches &&
           IRMath::abs(zoom - spec.expectedZoom_) < .0002f && editorMatches;
}

inline Recipe buildSpatialNavigation() {
    using Type = IRVideo::GuiInputEvent::Type;
    using Op = SpatialNavigationOperation;
    using Phase = IRWindow::PointerGesturePhase;
    Recipe recipe;
    recipe.name_ = "spatial_navigation";
    static constexpr std::array indices{0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
                                        10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    for (int shot = 0; shot < kSpatialNavigationShotCount; ++shot) {
        const auto &spec = spatialNavigationCases()[shot];
        Segment segment;
        segment.label_ = spec.label_;
        segment.zoom_ = spec.initialZoom_;
        const auto viewport = spatialNavigationAim();
        const auto panel = paletteSwatchCenterGuiTrixel(4);
        const auto move = [&](int frame, IRMath::vec2 point) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = Type::MOVE;
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            segment.events_.push_back(event);
        };
        const auto key = [&](int frame, Type type, IRInput::KeyMouseButtons button) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = type;
            event.button_ = button;
            segment.events_.push_back(event);
        };
        const auto pointer = [&](int frame, IRMath::vec2 point, Phase phase, IRMath::vec2 delta) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = frame;
            event.type_ = Type::POINTER_GESTURE;
            auto &gesture = event.pointerGesture_;
            const bool pinch = spec.operation_ == Op::PINCH || spec.operation_ == Op::UI_PINCH;
            const bool wheel = spec.operation_ == Op::WHEEL || spec.operation_ == Op::UI_WHEEL;
            gesture.kind_ = pinch ? IRWindow::PointerGestureKind::MAGNIFY
                                  : IRWindow::PointerGestureKind::SCROLL;
            gesture.source_ = wheel ? IRWindow::PointerGestureSource::WHEEL
                                    : IRWindow::PointerGestureSource::PRECISE_SCROLL;
            gesture.phase_ = phase;
            gesture.scroll_ = pinch ? IRMath::dvec2(0) : IRMath::dvec2(delta);
            gesture.magnification_ = pinch ? delta.y : 0.0;
            if (spec.operation_ == Op::PRECISE_PAN)
                gesture.modifiers_ = IRInput::kModifierShift;
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            segment.events_.push_back(event);
        };
        const bool startsOnPanel =
            spec.operation_ == Op::UI_ORBIT || spec.operation_ == Op::UI_PINCH ||
            spec.operation_ == Op::UI_START || spec.operation_ == Op::UI_WHEEL;
        const auto start = startsOnPanel ? panel : viewport;
        move(0, start);
        switch (spec.operation_) {
        case Op::CURSOR_DRAG:
        case Op::ROLL_DRAG:
        case Op::MODIFIER_RELEASE:
        case Op::SPACE_PAN: {
            const auto modifier = spec.operation_ == Op::SPACE_PAN ? IRInput::kKeyButtonSpace
                                                                   : IRInput::kKeyButtonLeftAlt;
            key(2, Type::PRESS, modifier);
            if (spec.operation_ == Op::ROLL_DRAG)
                key(3, Type::PRESS, IRInput::kKeyButtonLeftShift);
            key(4, Type::PRESS, IRInput::kMouseButtonLeft);
            move(6, viewport + spec.delta_);
            if (spec.operation_ == Op::MODIFIER_RELEASE) {
                key(8, Type::RELEASE, modifier);
                move(10, viewport + spec.delta_ + IRMath::vec2(30, 20));
            }
            key(12, Type::RELEASE, IRInput::kMouseButtonLeft);
            if (spec.operation_ != Op::MODIFIER_RELEASE)
                key(14, Type::RELEASE, modifier);
            if (spec.operation_ == Op::ROLL_DRAG)
                key(15, Type::RELEASE, IRInput::kKeyButtonLeftShift);
            break;
        }
        case Op::RESET_KEY:
            key(2, Type::PRESS, IRInput::kKeyButtonHome);
            key(4, Type::RELEASE, IRInput::kKeyButtonHome);
            break;
        case Op::RESET_BUTTON:
            move(2, {1012, 69});
            key(4, Type::PRESS, IRInput::kMouseButtonLeft);
            key(6, Type::RELEASE, IRInput::kMouseButtonLeft);
            break;
        case Op::WHEEL:
        case Op::UI_WHEEL:
            pointer(3, start, Phase::NONE, spec.delta_);
            break;
        default: {
            pointer(2, start, Phase::BEGAN, {});
            const auto end = spec.operation_ == Op::UI_START      ? viewport
                             : spec.operation_ == Op::UI_CROSSING ? panel
                                                                  : start;
            move(3, end);
            pointer(4, end, Phase::CHANGED, spec.delta_);
            pointer(8, end, Phase::ENDED, {});
            break;
        }
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateSpatialNavigation,
                &indices[shot],
                "spatial_pose_zoom_ownership_and_authored_state"
            )
        );
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateSpatialNavigationSurface,
                nullptr,
                "displayed_signed_faces_match_picking"
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SPATIAL_NAVIGATION_SESSION_H */
