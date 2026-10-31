#ifndef IR_VOXEL_EDITOR_SESSION_BUILDER_H
#define IR_VOXEL_EDITOR_SESSION_BUILDER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/input/ir_input_types.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/voxel/sdf_fill.hpp>
#include <irreden/common/array_transforms.hpp>

#include "anim_panel.hpp"
#include "array_panel.hpp"
#include "bake_panel.hpp"
#include "component_records.hpp"
#include "components_panel.hpp"
#include "lod_panel.hpp"
#include "loft_panel.hpp"
#include "module_session_view.hpp"
#include "panel_layout.hpp"
#include "palette.hpp"
#include "part_size_panel.hpp"
#include "parts_panel.hpp"
#include "recipes_panel.hpp"
#include "symmetry.hpp"

#include <algorithm>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Authoring sessions — compile a recipe of editor gestures into
// the scripted-input streams the GUI-test harness replays against the live UI.
//
// The entities are authored *by using the editor*: every voxel lands because
// a scripted cursor clicked a face and the editor's own place/erase path ran.
// So a recipe never touches voxel storage. It names cells; the builder works
// out which face of which already-placed voxel to click, aims the cursor with
// IRRender::worldPos3DToMouseScreenPxExact, and emits MOVE / PRESS / RELEASE
// events.
//
// The shadow occupancy model is what makes that aiming reliable. It mirrors the
// editable set's occupancy as the recipe grows it and casts the editor's own
// pick ray (IRPrefab::Picking::castGridRay) over that mirror, so an aim that
// would be occluded is caught while the recipe is being built rather than as a
// mystery FAIL (or, worse, a silent no-op) at run time.
//
// Two properties of the editor's isometric view drive the design:
//   - The picking ray runs along +(1,1,1), so exactly three faces of a voxel
//     can ever be clicked: -x, -y, -z. Placing at cell T therefore means
//     clicking face n of anchor cell T - n for one of those three normals.
//   - The pick resolves the face drawn under the cursor, so an aim has to sit
//     on a part of the face that is actually visible, with margin: a neighbour
//     one step toward the camera covers exactly half of a face, and its
//     silhouette then runs through the face's centre.
//
// Recipes run at the cardinal baseline yaw (no camera rotation between
// segments), which is what lets the model assume the (1,1,1) ray.
namespace IRVoxelEditor::Session {

// The three faces whose outward normal points back at the camera — the only
// faces a click can land on. Ordered x, y, z: the aim search prefers the side
// faces, so a column grown upward is still reachable from the side later.
inline constexpr IRMath::ivec3 kCameraFacingNormals[3] = {
    IRMath::ivec3(-1, 0, 0),
    IRMath::ivec3(0, -1, 0),
    IRMath::ivec3(0, 0, -1),
};

// Candidate aim points on a face, as offsets from its centre along its two
// in-plane axes: the centre, then the centre of each quadrant.
inline constexpr IRMath::vec2 kFaceAimOffsets[5] = {
    IRMath::vec2(0.0f, 0.0f),
    IRMath::vec2(-0.25f, -0.25f),
    IRMath::vec2(0.25f, -0.25f),
    IRMath::vec2(-0.25f, 0.25f),
    IRMath::vec2(0.25f, 0.25f),
};

// Half-width, in voxels, of the clear patch an aim must sit in the middle of.
// An eighth of a voxel is a screen pixel at zoom 4 and half of one at zoom 2 —
// the most the cursor's rounding to a whole pixel can move it.
inline constexpr float kFaceAimMargin = 0.125f;

// The pick ray's per-axis step at the cardinal camera the recipes author at.
// Sessions never rotate the camera, so the model casts along it unconditionally.
inline constexpr IRMath::ivec3 kSessionRayDirection = IRMath::ivec3(1);

// Frames a session shot leaves between the cursor MOVE and the button PRESS,
// and again before the RELEASE. One frame each is what the existing scripted
// shots use (kPaletteClickEvents); the drag ops add a HELD frame on top so the
// editor's drag state machine sees a PRESSED, a HELD, and a RELEASED sample.
inline constexpr int kFramesPerClickStep = 1;

// A second press on a slider within this many frames of the first reads as a
// double click (system_widget_input.hpp) and restores the authored default
// instead of dragging. A drag on a slider this recently pressed idles first.
inline constexpr int kSliderRepressIdleFrames = 19;

// Camera zoom sessions author at by default: every face is 8 x 8 screen pixels,
// so a capture reads clearly. A face aim resolves down to zoom 1.
inline constexpr float kSessionZoom = 4.0f;

// How far, per axis, a recipe handle's `at` may sit from a whole cell for
// dragRecipeHandle to name the cell the editor reads at the press. A press
// lands within a pixel of `at`, an eighth of a cell at the session zoom.
inline constexpr float kRecipeHandlePressSlack = 0.25f;

// How far a dragRecipeHandle target's world coordinate sum may sit from its
// `at`'s: float error from turning both with the part.
inline constexpr float kRecipeHandlePlaneSlack = 1e-3f;

// Keep this above the live candidate count so the command-owned Tab walk proves
// that editor commands do not hand focus to a widget.
inline constexpr int kTabWalkPresses = 80;
using ::IRVoxelEditor::kSkeletonPanelPos;
using ::IRVoxelEditor::kSkeletonPanelSize;
inline constexpr IRMath::ivec2 kJointRenameInputPos{
    kSkeletonPanelPos.x + 4, kSkeletonPanelPos.y + 74
};
inline constexpr IRMath::ivec2 kJointRenameInputSize{82, 14};
inline constexpr IRMath::vec2 kJointRenameInputCenter{
    static_cast<float>(kJointRenameInputPos.x) + static_cast<float>(kJointRenameInputSize.x) / 2.0f,
    static_cast<float>(kJointRenameInputPos.y) + static_cast<float>(kJointRenameInputSize.y) / 2.0f
};

// Mirror of the editable set's occupancy, in local cell coordinates. Carries
// the scene origin so it can convert to the world positions the aim math and
// the picker both work in.
class OccupancyModel {
  public:
    OccupancyModel(IRMath::ivec3 size, IRMath::vec3 origin)
        : m_size(size)
        , m_origin(origin)
        , m_occupied(static_cast<std::size_t>(size.x) * size.y * size.z, false) {}

    bool inBounds(IRMath::ivec3 local) const {
        return local.x >= 0 && local.x < m_size.x && local.y >= 0 && local.y < m_size.y &&
               local.z >= 0 && local.z < m_size.z;
    }

    bool occupied(IRMath::ivec3 local) const {
        if (!inBounds(local))
            return false;
        return m_occupied[flatIndex(local)];
    }

    void set(IRMath::ivec3 local, bool on) {
        if (!inBounds(local))
            return;
        m_occupied[flatIndex(local)] = on;
    }

    // Mirror-symmetry-aware writes. The editor's applyEdit
    // reflects every edit across the enabled mirror planes (via applyMirrors);
    // the shadow model must mirror the same way, or aiming a later click at a
    // mirror-created voxel (or asserting its occupancy) would disagree with the
    // live set. Both sides call the one applyMirrors helper against the same
    // scene-centre plane, so they can't drift.
    void setMirrored(IRMath::ivec3 cell, bool on, const SymmetryState &sym) {
        if (!sym.enableX_ && !sym.enableY_ && !sym.enableZ_) {
            set(cell, on);
            return;
        }
        std::vector<IRMath::ivec3> cells;
        applyMirrors(cell, sym, cells);
        for (const IRMath::ivec3 &c : cells)
            set(c, on);
    }

    void setBoxMirrored(IRMath::ivec3 a, IRMath::ivec3 b, bool on, const SymmetryState &sym) {
        const IRMath::ivec3 lo{IRMath::min(a.x, b.x), IRMath::min(a.y, b.y), IRMath::min(a.z, b.z)};
        const IRMath::ivec3 hi{IRMath::max(a.x, b.x), IRMath::max(a.y, b.y), IRMath::max(a.z, b.z)};
        IRMath::iterateAABB(lo, hi, [&](int x, int y, int z) {
            setMirrored(IRMath::ivec3(x, y, z), on, sym);
        });
    }

    IRMath::ivec3 size() const {
        return m_size;
    }

    IRMath::vec3 origin() const {
        return m_origin;
    }

    void setOrigin(IRMath::vec3 origin) {
        m_origin = origin;
    }

    // The turn about the part's pivot (the middle of its box) that a radial
    // array's COPY YAW gave it, and the render mode selectPartMode chose. Only
    // recipe-handle drags read them; the edit pick models an unturned part.
    IRMath::vec4 rotation() const {
        return m_rotation;
    }

    void setRotation(IRMath::vec4 rotation) {
        m_rotation = rotation;
    }

    void setMode(IRComponents::RotationMode mode) {
        m_mode = mode;
    }

    // Whether the editor can rewrite this part's cells in place, so draws its
    // recipe handles: a turned GRID part re-voxelizes its span.
    bool cellsEditableInPlace() const {
        return m_mode != IRComponents::RotationMode::GRID ||
               m_rotation == IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Where the editor draws set-local point @p local of an unscaled part:
    // turned with the part about its pivot.
    IRMath::vec3 worldPoint(IRMath::vec3 local) const {
        const IRMath::vec3 pivotToOrigin =
            IRComponents::anchorOffset(IRComponents::EntityAnchor::CENTER, m_size);
        return m_origin - pivotToOrigin +
               IRMath::rotateVectorByQuat(pivotToOrigin + local, m_rotation);
    }

    bool sameOccupancy(const OccupancyModel &other) const {
        return m_size == other.m_size && m_occupied == other.m_occupied;
    }

    int occupiedCount() const {
        return static_cast<int>(std::count(m_occupied.begin(), m_occupied.end(), true));
    }

    // Places every cell the SDF primitive covers: the same cell walk the
    // editor's BAKE fills through.
    void bakeShape(IRMath::SDF::ShapeType type, IRMath::vec4 params, const SymmetryState &sym) {
        IRPrefab::Voxel::forEachSdfGridCell(
            m_size,
            type,
            params,
            [&](IRMath::ivec3 local, std::size_t) { setMirrored(local, true, sym); }
        );
    }

    // The single-set editor seeds its editable set with a full ground plane at
    // the far z slice so the first click has something to land on (main.cpp
    // initEntities). An entity-scene voxel part starts clear.
    void seedGroundPlane() {
        const int z = m_size.z - 1;
        for (int y = 0; y < m_size.y; ++y)
            for (int x = 0; x < m_size.x; ++x)
                set(IRMath::ivec3(x, y, z), true);
    }

    IRMath::vec3 worldCenter(IRMath::ivec3 local) const {
        return m_origin + IRMath::vec3(local);
    }

    // What the editor's edit pick reports for a cursor parked on `worldAim`:
    // the cell the ray lands on and the face it enters through. The same
    // traversal the live pick runs, over the mirror instead of the set.
    std::optional<IRPrefab::Picking::GridRayHit> pick(IRMath::vec3 worldAim) const {
        return IRPrefab::Picking::castGridRay(
            worldAim - m_origin,
            kSessionRayDirection,
            m_size,
            [this](IRMath::ivec3 local) { return occupied(local); }
        );
    }

    // Where to put the cursor to click `local`'s `normal` face, or nullopt when
    // no candidate point on it is visible with margin. Candidates are the face
    // centre, then the centre of each quadrant; one is taken only if the points
    // kFaceAimMargin either side of it along both in-plane axes pick the same
    // cell, so the emitted cursor's rounding to a whole screen pixel cannot
    // carry it across a silhouette.
    std::optional<IRMath::vec3> faceAim(IRMath::ivec3 local, IRMath::ivec3 normal) const {
        const int normalAxis = normal.x != 0 ? 0 : (normal.y != 0 ? 1 : 2);
        IRMath::vec3 tangentU(0.0f);
        IRMath::vec3 tangentV(0.0f);
        tangentU[(normalAxis + 1) % 3] = 1.0f;
        tangentV[(normalAxis + 2) % 3] = 1.0f;
        const IRMath::vec3 faceCentre = worldCenter(local) + IRMath::vec3(normal) * 0.5f;
        for (const IRMath::vec2 &offset : kFaceAimOffsets) {
            const IRMath::vec3 aim = faceCentre + tangentU * offset.x + tangentV * offset.y;
            bool clear = picksCell(aim, local);
            for (const float du : {-kFaceAimMargin, kFaceAimMargin})
                for (const float dv : {-kFaceAimMargin, kFaceAimMargin})
                    clear = clear && picksCell(aim + tangentU * du + tangentV * dv, local);
            if (clear)
                return aim;
        }
        return std::nullopt;
    }

    // Where to aim to click `target` itself (erase / drag over existing
    // geometry). Returns nullopt when every camera-facing face of `target` is
    // occluded — the caller reports that as a recipe error rather than emitting
    // a click that would silently edit the wrong cell.
    std::optional<IRMath::vec3> aimAtVoxel(IRMath::ivec3 target) const {
        if (!occupied(target))
            return std::nullopt;
        for (const IRMath::ivec3 &normal : kCameraFacingNormals) {
            if (const std::optional<IRMath::vec3> aim = faceAim(target, normal))
                return aim;
        }
        return std::nullopt;
    }

    // Where to aim to click the `normal` face of `anchor`, or nullopt when
    // that face is occluded or `normal` does not face the camera.
    std::optional<IRMath::vec3> aimAtFace(IRMath::ivec3 anchor, IRMath::ivec3 normal) const {
        return occupied(anchor) ? faceAim(anchor, normal) : std::nullopt;
    }

    // Where to aim so a place-mode click lands a voxel at `target`. The editor
    // places at (hit voxel + hit face normal), so the anchor is target - normal.
    std::optional<IRMath::vec3> aimToPlace(IRMath::ivec3 target) const {
        return aimToPlaceThroughAnchor(target, false);
    }

    // Where to aim so an Alt+click lands a voxel at `target`. The editor places
    // at (hit voxel - hit face normal) with Alt held, so the anchor is
    // target + normal: one step TOWARD the camera, i.e. precisely the voxel that
    // occludes the target. That is what makes these cells unreachable without
    // the modifier — and why it is the anchor's visibility, never the target's,
    // that has to be checked.
    std::optional<IRMath::vec3> aimToPlaceBelow(IRMath::ivec3 target) const {
        return aimToPlaceThroughAnchor(target, true);
    }

  private:
    // Shared body of aimToPlace / aimToPlaceBelow. `inverted` is the editor's own
    // Alt flag (main.cpp's editTargetCell), and it moves exactly one thing: which
    // side of `target` the anchor sits on. Everything else is common — the anchor
    // must be occupied, the target in-bounds and empty, and the anchor's face
    // toward the target aimable. The first normal that satisfies all three wins.
    std::optional<IRMath::vec3> aimToPlaceThroughAnchor(IRMath::ivec3 target, bool inverted) const {
        if (!inBounds(target) || occupied(target))
            return std::nullopt;
        for (const IRMath::ivec3 &normal : kCameraFacingNormals) {
            const IRMath::ivec3 anchor = inverted ? target + normal : target - normal;
            if (!occupied(anchor))
                continue;
            if (const std::optional<IRMath::vec3> aim = faceAim(anchor, normal))
                return aim;
        }
        return std::nullopt;
    }

    bool picksCell(IRMath::vec3 worldAim, IRMath::ivec3 local) const {
        const std::optional<IRPrefab::Picking::GridRayHit> hit = pick(worldAim);
        return hit && hit->cell_ == local;
    }

    std::size_t flatIndex(IRMath::ivec3 local) const {
        return static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, m_size));
    }

    IRMath::ivec3 m_size;
    IRMath::vec3 m_origin;
    IRMath::vec4 m_rotation = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    IRComponents::RotationMode m_mode = IRComponents::RotationMode::GRID;
    std::vector<bool> m_occupied;
};

// One cursor MOVE whose pixel is resolved at shot-run time. The world→screen
// mapping reads the live camera (zoom, iso offset, letterbox), so the pixel
// cannot be baked at recipe-build time — the editor fills it in the harness's
// per-frame assert callback, exactly as the probe-map shots do.
struct AimFixup {
    int eventIndex_ = 0;
    IRMath::vec3 worldPoint_ = IRMath::vec3(0.0f);
};

// One cursor MOVE onto a GUI-canvas widget, resolved at shot-run time the same
// way an AimFixup is. The GUI canvas is sized from the live framebuffer
// (setGuiCanvasFullResolution), so a widget's screen pixel is no more bakeable
// at recipe-build time than a scene voxel's is.
struct GuiAimFixup {
    int eventIndex_ = 0;
    IRMath::vec2 guiTrixel_ = IRMath::vec2(0.0f);
};

// Occupancy expectation evaluated against the real editable set at a shot's
// capture frame. This is what makes a session positive-fire: a recipe that
// silently no-ops (click swallowed by a widget, aim occluded by scene furniture,
// gesture never reaching the place path) fails here instead of quietly saving
// an empty asset.
// Which copy of "is this cell live" a check reads. The per-voxel alpha is the
// CPU-side truth; the pool's active mask is the GPU-side mirror the compact
// shader reads *instead of* alpha, and it is pool state rather than
// voxel-record state — so a raw write to a set's `voxels_` span updates one and
// not the other. A recipe that steps animation frames asserts BOTH: the two
// disagreeing is exactly the shape of a missing resyncAfterRawEdits.
enum class CheckSource { VOXEL_ALPHA, POOL_ACTIVE_MASK };

struct OccupancyCheck {
    IRMath::ivec3 localCell_ = IRMath::ivec3(0);
    int partIndex_ = -1;
    bool expectOccupied_ = false;
    CheckSource source_ = CheckSource::VOXEL_ALPHA;
    // Set only by expectVoxelColor: the RGB the cell's voxel must carry on top
    // of being occupied. Alpha is deliberately not compared — it is the
    // occupancy channel (layer visibility drives it), which expectOccupied_
    // already covers.
    std::optional<IRMath::Color> expectColor_;
    std::string name_;
};

struct PartTransformCheck {
    int partIndex_ = 0;
    IRMath::vec3 expected_ = IRMath::vec3(0.0f);
    float tolerance_ = 0.001f;
    bool expectEqual_ = true;
    std::string name_;
};

struct PartCountCheck {
    int expected_ = 0;
    std::string name_;
};

struct PartIdCheck {
    int partIndex_ = 0;
    std::string expected_;
    std::string name_;
};

struct PartAuthoredCountCheck {
    int partIndex_ = 0;
    int expected_ = 0;
    std::string name_;
};

struct PartClassMaskCheck {
    int partIndex_ = 0;
    IRMath::ivec3 local_ = IRMath::ivec3(0);
    std::optional<std::string> expected_;
    std::string name_;
};

struct PartEditableCheck {
    int partIndex_ = 0;
    bool expected_ = true;
    std::string name_;
};

struct CanvasCountCheck {
    int expectedOffset_ = 0;
    std::string name_;
};

struct RotationModeCheck {
    int partIndex_ = 0;
    IRComponents::RotationMode expected_ = IRComponents::RotationMode::GRID;
    std::string name_;
};

// Anchor expectation on one entity-scene voxel part: its C_VoxelSetNew::anchor_.
struct PartAnchorCheck {
    int partIndex_ = 0;
    IRComponents::EntityAnchor expected_ = IRComponents::EntityAnchor::CORNER;
    std::string name_;
};

// Yaw expectation on one entity-scene part: the angle its C_LocalTransform
// rotation turns about +z, in degrees, within toleranceDegrees_ of
// expectedDegrees_ on the circle.
struct PartYawCheck {
    int partIndex_ = 0;
    float expectedDegrees_ = 0.0f;
    float toleranceDegrees_ = 0.5f;
    std::string name_;
};

// Turn expectation on one entity-scene part: its C_LocalTransform rotation
// is a turn of at least minDegrees_ about plus or minus axis_, a unit vector
// in the part's own frame. The sign and size of a gizmo ring drag follow the
// cursor's path, so only the axis is exact.
struct PartTurnAxisCheck {
    int partIndex_ = 0;
    IRMath::vec3 axis_ = IRMath::vec3(0.0f, 0.0f, 1.0f);
    float minDegrees_ = 0.0f;
    std::string name_;
};

// Gizmo drag expectation: GIZMO_DRAG holds an active drag when the segment
// settles, or with expectActive_ false, holds none.
struct GizmoDragCheck {
    bool expectActive_ = false;
    std::string name_;
};

// Gizmo handle census: the main-canvas texels whose entity id is a gizmo
// handle. A check with no baseline_ records them and needs at least one; a
// check naming one must read exactly the texels it recorded.
struct GizmoTexelCheck {
    const GizmoTexelCheck *baseline_ = nullptr;
    mutable std::vector<std::size_t> texels_;
    std::string name_;
};

// Recipe-binding expectations on one entity-scene part. A RecipeValueCheck
// reads the bound value of param_ (the part must be bound to a recipe that
// declares it); a PartRecipeMatchCheck holds when the part's filled cells are
// exactly its bound recipe's cells at its bound values.
struct RecipeValueCheck {
    int partIndex_ = 0;
    std::string param_;
    float expected_ = 0.0f;
    std::string name_;
};

struct PartRecipeMatchCheck {
    int partIndex_ = 0;
    std::string name_;
};

// Whether the selected part's recipe handle handle_ owns any main-canvas
// texel.
struct RecipeHandleTexelCheck {
    std::string handle_;
    bool expectDrawn_ = false;
    std::string name_;
};

// Spin expectation on one entity-scene part: it carries C_AutoSpin turning
// radiansPerFrame_ a tick, or with expectEqual_ false, it does not (an absent
// component counts as not equal).
struct PartSpinCheck {
    int partIndex_ = 0;
    float radiansPerFrame_ = 0.0f;
    bool expectEqual_ = true;
    std::string name_;
};

// PART MODE dropdown expectation: it is collapsed and shows expected_'s row.
struct PartModeDropdownCheck {
    IRComponents::RotationMode expected_ = IRComponents::RotationMode::GRID;
    std::string name_;
};

enum class EditorInputCheckKind : int {
    SELECTED_PART_NO_TEXT_FOCUS = 0,
    TEXT_CAPTURED_X = 1,
    TEXT_FOCUS_RELEASED = 2,
    X_SYMMETRY_ENABLED = 3,
    TAB_WALK_COVERAGE = 4,
};

struct EditorInputCheck {
    EditorInputCheckKind kind_ = EditorInputCheckKind::SELECTED_PART_NO_TEXT_FOCUS;
    int expectedPart_ = -1;
    std::string name_;
};

// PARTS list expectation. expectedSelected_ >= 0: the list widget and the
// entity scene both select that part, and its row is one the list paints.
// expectedTop_ >= 0: that part is the list's first visible item. -1 leaves
// either unchecked.
struct PartsListCheck {
    int expectedSelected_ = -1;
    int expectedTop_ = -1;
    std::string name_;
};

// Camera-zoom expectation: the live zoom equals zoom_, or with expectEqual_
// false, differs from it.
struct CameraZoomCheck {
    float zoom_ = kSessionZoom;
    bool expectEqual_ = true;
    std::string name_;
};

// Pick expectation evaluated through the editor's own edit pick at a shot's
// capture frame: the world voxel the parked cursor must land on.
struct PickCheck {
    IRMath::ivec3 worldVoxel_ = IRMath::ivec3(0);
    std::string name_;
};

// Fill-preview expectation, read from the main canvas's entity ids at a shot's
// capture frame. With expectDrawn_ the ghost shape is flagged visible, owns at
// least one texel, and every texel it owns lies on a view ray through the box
// of world cells worldLo_..worldHi_; without it the ghost is hidden and owns
// none. Owned texels are the ones the ghost is the nearest surface on, so a
// ghost inside the voxels it would carve owns none while it is drawn.
struct FillGhostCheck {
    bool expectDrawn_ = false;
    IRMath::ivec3 worldLo_ = IRMath::ivec3(0);
    IRMath::ivec3 worldHi_ = IRMath::ivec3(0);
    std::string name_;
};

// Which ANIM or LOD panel slider a SliderCheck reads. Named rather than
// carrying the widget's EntityId directly: Session::build runs well before
// initEntities creates the widgets, so the id isn't known yet at recipe-build
// time — the check can only name which slider and defer the lookup to
// evaluateSliderCheck.
enum class SliderTarget { FPS, SCRUBBER, LOD_FINE, LOD_COARSE, LOD_TIER };

// Slider-value expectation evaluated against the live widget at a shot's
// capture frame.
struct SliderCheck {
    SliderTarget target_ = SliderTarget::FPS;
    float expected_ = 0.0f;
    float tolerance_ = 0.001f;
    std::string name_;
};

// Registry expectation for a module-registered component: present in the
// Lua-typed component enumeration with this many fields.
struct ComponentCheck {
    std::string componentName_;
    int fieldCount_ = 0;
    std::string name_;
};

// LOD-gate expectation on one entity-scene part: whether the engine's DENSE
// LOD gate holds the part out of the draw, and the tier the part is pinned to
// (-1: no C_LodTierOverride, so it follows the camera-zoom tier).
struct PartGateCheck {
    int partIndex_ = 0;
    bool expectGated_ = false;
    int expectPinnedTier_ = -1;
    std::string name_;
};

// Extent expectation on one entity-scene voxel part: its C_VoxelSetNew::size_.
struct PartExtentCheck {
    int partIndex_ = 0;
    IRMath::ivec3 expected_ = IRMath::ivec3(0);
    std::string name_;
};

// Mirror expectation: the editor's enabled axes, and the plane each enabled
// axis is seated at. A disabled axis's offset is not compared.
struct MirrorCheck {
    SymmetryState expected_;
    std::string name_;
};

enum class LoftMask { XZ, YZ };

// Loft-mask expectation: whether cell_ (h, v) of one mask is painted.
struct LoftMaskCheck {
    LoftMask mask_ = LoftMask::XZ;
    IRMath::ivec2 cell_ = IRMath::ivec2(0);
    bool expectOn_ = false;
    std::string name_;
};

// The last-saved entity-scene manifest contains text_ and was written by this
// run, so a manifest left by an earlier run cannot satisfy it.
struct ManifestCheck {
    std::string text_;
    bool expectContains_ = true;
    std::string name_;
};

// Module-panel expectation: the docked panel named panelName_ draws a
// label reading label_.
struct PanelLabelCheck {
    std::string panelName_;
    std::string label_;
    std::string name_;
};

struct RecipePagerCheck {
    bool present_ = false;
    std::string name_;
};

// Component expectation on an entity-scene target (a part index, or
// kEntitySceneRootTarget), read from the live entity: field_ of component_
// holds expected_. A nullopt expected_ asserts the component is absent.
struct ComponentValueCheck {
    int target_ = 0;
    std::string component_;
    std::string field_;
    std::optional<ComponentFieldValue> expected_;
    bool expectEqual_ = true;
    std::string name_;
};

// One shot's worth of session: a camera framing, the events that fire under it,
// their aim fixups, and the assertions evaluated once it settles.
struct Segment {
    std::string label_;
    float zoom_ = kSessionZoom;
    IRMath::vec2 cameraIso_ = IRMath::vec2(0.0f);
    std::vector<IRVideo::GuiInputEvent> events_;
    std::vector<AimFixup> aims_;
    std::vector<GuiAimFixup> guiAims_;
    std::vector<IRPrefab::GuiTest::Assertion> assertions_;
};

// A built session. `shots_` points into `segments_`, so neither collection may
// be mutated after resolveShots() — the harness holds the pointers for the whole
// run (GuiTestConfig's caller-owned-table contract).
struct Recipe {
    std::string name_;
    std::vector<Segment> segments_;
    std::vector<IRVideo::GuiTestShot> shots_;
    // Stable storage for the assertion predicates' context. std::deque, not
    // vector: assertions hold pointers into it and it grows as ops are added.
    std::deque<OccupancyCheck> checks_;
    std::deque<PartTransformCheck> partTransformChecks_;
    std::deque<PartCountCheck> partCountChecks_;
    std::deque<PartIdCheck> partIdChecks_;
    std::deque<PartAuthoredCountCheck> partAuthoredCountChecks_;
    std::deque<PartClassMaskCheck> partClassMaskChecks_;
    std::deque<PartEditableCheck> partEditableChecks_;
    std::deque<CanvasCountCheck> canvasCountChecks_;
    std::deque<RotationModeCheck> rotationModeChecks_;
    std::deque<PartAnchorCheck> partAnchorChecks_;
    std::deque<PartYawCheck> partYawChecks_;
    std::deque<PartTurnAxisCheck> partTurnAxisChecks_;
    std::deque<GizmoDragCheck> gizmoDragChecks_;
    std::deque<GizmoTexelCheck> gizmoTexelChecks_;
    std::deque<RecipeValueCheck> recipeValueChecks_;
    std::deque<PartRecipeMatchCheck> partRecipeMatchChecks_;
    std::deque<RecipeHandleTexelCheck> recipeHandleTexelChecks_;
    std::deque<PartSpinCheck> partSpinChecks_;
    std::deque<PartModeDropdownCheck> partModeDropdownChecks_;
    std::deque<EditorInputCheck> editorInputChecks_;
    std::deque<PartsListCheck> partsListChecks_;
    std::deque<CameraZoomCheck> cameraZoomChecks_;
    // Same stable-storage contract as checks_, for expectSliderValue.
    std::deque<SliderCheck> sliderChecks_;
    // Same stable-storage contract as checks_, for expectPick.
    std::deque<PickCheck> pickChecks_;
    // Same contract, for expectFillGhost and expectNoFillGhost.
    std::deque<FillGhostCheck> fillGhostChecks_;
    // Same contract, for the module checks.
    std::deque<ComponentCheck> componentChecks_;
    std::deque<PanelLabelCheck> panelLabelChecks_;
    std::deque<RecipePagerCheck> recipePagerChecks_;
    std::deque<ComponentValueCheck> componentValueChecks_;
    // Same contract, for the entity-scene LOD checks.
    std::deque<PartGateCheck> partGateChecks_;
    std::deque<ManifestCheck> manifestChecks_;
    // Same contract, for the part-size, mirror and loft checks.
    std::deque<PartExtentCheck> partExtentChecks_;
    std::deque<MirrorCheck> mirrorChecks_;
    std::deque<LoftMaskCheck> loftMaskChecks_;
    // Build the editor's reference furniture (floor slab, axis bars, centre
    // cube, perimeter gizmos, starter rig, satellite sets) around the editable
    // set instead of the bare stage entity recipes author on.
    bool referenceFurniture_ = false;
    // Recipe errors (an unreachable cell, an occluded aim). Non-empty means the
    // session is not runnable; the editor logs these and exits rather than
    // replaying a stream that would author the wrong thing.
    std::vector<std::string> errors_;

    bool ok() const {
        return errors_.empty();
    }
};

// Fills @p recipe's shot table. Each shot points at its segment's event vector
// and label string, so this must run once the recipe is in the storage it will
// live in for the whole run — resolving it inside the builder and then moving
// the recipe into place would leave the labels pointing at moved-from short
// strings. Call exactly once, and treat the segments as frozen afterward.
inline void resolveShots(Recipe &recipe) {
    recipe.shots_.clear();
    recipe.shots_.reserve(recipe.segments_.size());
    for (const Segment &segment : recipe.segments_) {
        IRVideo::GuiTestShot shot{};
        shot.render_.zoom_ = segment.zoom_;
        shot.render_.cameraIso_ = segment.cameraIso_;
        shot.render_.label_ = segment.label_.c_str();
        shot.inputs_ = segment.events_.data();
        shot.numInputs_ = static_cast<int>(segment.events_.size());
        recipe.shots_.push_back(shot);
    }
}

// Reads one OccupancyCheck against the live editable set. Wired as a
// GuiTest PREDICATE assertion so session state checks share the harness's
// single GUI-ASSERT emitter instead of hand-rolling the log line per check.
// Defined in main.cpp, where the editable-set entity handle lives.
bool evaluateOccupancyCheck(const void *context, std::string &actual);
bool evaluatePartTransformCheck(const void *context, std::string &actual);
bool evaluatePartCountCheck(const void *context, std::string &actual);
bool evaluatePartIdCheck(const void *context, std::string &actual);
bool evaluatePartAuthoredCountCheck(const void *context, std::string &actual);
bool evaluatePartClassMaskCheck(const void *context, std::string &actual);
bool evaluatePartEditableCheck(const void *context, std::string &actual);
bool evaluateCanvasCountCheck(const void *context, std::string &actual);
bool evaluateRotationModeCheck(const void *context, std::string &actual);
bool evaluatePartAnchorCheck(const void *context, std::string &actual);
bool evaluatePartYawCheck(const void *context, std::string &actual);
bool evaluatePartTurnAxisCheck(const void *context, std::string &actual);
bool evaluateGizmoDragCheck(const void *context, std::string &actual);
bool evaluateGizmoTexelCheck(const void *context, std::string &actual);
bool evaluateRecipeValueCheck(const void *context, std::string &actual);
bool evaluatePartRecipeMatchCheck(const void *context, std::string &actual);
bool evaluateRecipeHandleTexelCheck(const void *context, std::string &actual);
bool evaluatePartSpinCheck(const void *context, std::string &actual);
bool evaluatePartModeDropdownCheck(const void *context, std::string &actual);
bool evaluateEditorInputCheck(const void *context, std::string &actual);

// Read one PartsListCheck / CameraZoomCheck. Defined in main.cpp, where the
// PARTS list widget and the entity scene live.
bool evaluatePartsListCheck(const void *context, std::string &actual);
bool evaluateCameraZoomCheck(const void *context, std::string &actual);

// Reads one PickCheck through the editor's edit pick. Same PREDICATE channel
// as evaluateOccupancyCheck. Defined in main.cpp, beside the pick itself.
bool evaluatePickCheck(const void *context, std::string &actual);

// Sweeps the rendered frame for agreement with the edit pick; takes no
// context. Defined in main.cpp.
bool evaluatePickMatchesRender(const void *context, std::string &actual);

// Reads one FillGhostCheck against the fill tool's ghost shape and the main
// canvas's entity ids. Defined in main.cpp, where the ghost entity lives.
bool evaluateFillGhostCheck(const void *context, std::string &actual);

// Reads one SliderCheck against the live widget its target_ names. Same
// PREDICATE channel as evaluateOccupancyCheck, for the same reason: the
// widget entity ids don't exist at recipe-build time. Defined in main.cpp,
// where the ANIM panel's slider entity handles live.
bool evaluateSliderCheck(const void *context, std::string &actual);

// Reads one ComponentCheck / PanelLabelCheck against the loaded module.
// Defined in main.cpp, where the module host and its docked panels live.
bool evaluateComponentCheck(const void *context, std::string &actual);
bool evaluatePanelLabelCheck(const void *context, std::string &actual);
bool evaluateRecipePagerCheck(const void *context, std::string &actual);

// Read one PartGateCheck / ManifestCheck against the live entity scene and the
// file its last save wrote. Defined in main.cpp, where the scene lives.
bool evaluatePartGateCheck(const void *context, std::string &actual);
bool evaluateManifestCheck(const void *context, std::string &actual);

// Reads one ComponentValueCheck against the entity scene. Defined in main.cpp,
// where the entity scene and the module's Lua state live.
bool evaluateComponentValueCheck(const void *context, std::string &actual);

// Read one PartExtentCheck / MirrorCheck / LoftMaskCheck against the live
// part, the editor's symmetry state and the loft masks. Defined in main.cpp.
bool evaluatePartExtentCheck(const void *context, std::string &actual);
bool evaluateMirrorCheck(const void *context, std::string &actual);
bool evaluateLoftMaskCheck(const void *context, std::string &actual);

// Builds a Recipe from editor gestures. Every op appends to the current
// segment; segment(label) closes the current one and starts the next. Ops that
// cannot be aimed record an error instead of emitting a bogus click.
class Builder {
  public:
    Builder(
        std::string name,
        IRMath::ivec3 sceneSize,
        IRMath::vec3 sceneOrigin,
        const ModuleSessionView *moduleView = nullptr
    )
        : m_model(sceneSize, sceneOrigin)
        , m_sceneSize(sceneSize)
        , m_partSize(sceneSize)
        , m_moduleView(moduleView)
        , m_recipeList(recipeListGeometry())
        , m_componentList(componentListGeometry()) {
        m_recipe.name_ = std::move(name);
        if (m_moduleView != nullptr) {
            for (const ModuleRecipe &recipe : m_moduleView->recipes_)
                m_recipeList.items_.push_back(recipe.name_);
            for (const ComponentRecord &component : m_moduleView->components_)
                m_componentList.items_.push_back(component.name_);
        }
        m_model.seedGroundPlane();
        segment("start");
    }

    IRMath::ivec3 sceneSize() const {
        return m_sceneSize;
    }

    IRMath::vec3 sceneOrigin() const {
        return m_model.origin();
    }

    // The shadow occupancy of the set the editor is editing at this point of
    // the recipe.
    const OccupancyModel &model() const {
        return m_model;
    }

    // Author against the editor's full reference scene rather than the bare
    // stage. The model is unchanged: the edit pick passes through furniture,
    // so only the editable set's occupancy decides where an aim lands. What
    // the model does not know is where a gizmo handle is drawn — a recipe
    // that asks for furniture keeps its aims off those pixels itself.
    void withReferenceFurniture() {
        m_recipe.referenceFurniture_ = true;
    }

    // Close the current segment and open a new one. The camera framing is
    // re-applied per shot, which is also how a session recovers from the A/D
    // frame keys nudging the camera.
    //
    // Segments are shot boundaries: a segment's assertions evaluate ONCE, at
    // end of segment, against the state after ALL of that segment's events
    // have fired — never interleaved with individual ops within it.
    // A hover()/expectPick() pre-arm check for one target added to the same
    // segment as a later destructive click() on a different target therefore
    // evaluates against the post-click state, even though it reads as firing
    // "before" the click in source order. To pre-arm a check before a click,
    // close the arm in its own segment (via segment(...)) before the click's
    // segment opens — the carve_arm / carve pair is the reference shape.
    void segment(
        const char *label, float zoom = kSessionZoom, IRMath::vec2 cameraIso = IRMath::vec2(0.0f)
    ) {
        flushSegment();
        m_current = Segment{};
        m_current.label_ = m_recipe.name_ + "_" + label;
        m_current.zoom_ = zoom;
        m_current.cameraIso_ = cameraIso;
        m_frame = 0;
        ++m_segmentIndex;
        m_cameraTurned = false;
    }

    // Single left click that places a voxel at `target` (place mode) or erases
    // the voxel at `target` (erase mode). Both are the editor's press-then-
    // release-without-moving gesture; the drag state machine resolves a
    // zero-length drag to one applyEdit.
    void click(IRMath::ivec3 target) {
        const std::optional<IRMath::vec3> aim = aimFor(target);
        if (!aim) {
            recordUnreachable("click", target);
            return;
        }
        emitClick(*aim);
        m_model.setMirrored(target, !m_eraseMode, m_symmetry);
    }

    // The place click for `target`, emitted where something drawn in front of
    // the scene is expected to take it — a gizmo handle over the anchor face.
    // The model is left alone; pair it with an expectOccupancy(target, false).
    void clickExpectingNoEdit(IRMath::ivec3 target) {
        const std::optional<IRMath::vec3> aim = aimFor(target);
        if (!aim) {
            recordUnreachable("clickExpectingNoEdit", target);
            return;
        }
        emitClick(*aim);
    }

    // Single left click on the `normal` face of `anchor`, placing the voxel in
    // front of it. click() reaches the same cell through whichever anchor it
    // finds first; this names the face, for a recipe whose point is that a
    // particular face resolves.
    void clickFace(IRMath::ivec3 anchor, IRMath::ivec3 normal) {
        const IRMath::ivec3 target = anchor + normal;
        const std::optional<IRMath::vec3> aim = m_eraseMode || m_model.occupied(target)
                                                    ? std::nullopt
                                                    : m_model.aimAtFace(anchor, normal);
        if (!aim) {
            recordUnreachable("clickFace", target);
            return;
        }
        emitClick(*aim);
        m_model.setMirrored(target, true, m_symmetry);
    }

    // Alt + single left click: places a voxel at `target` on the FAR side of the
    // clicked face. The only op that can reach a cell *below* standing geometry
    // — the plain gestures all grow toward the camera, because those are the
    // only faces the picker exposes (docs/design/editor-authoring-friction.md
    // §2g F-2g-1; the editor side is main.cpp's editTargetCell).
    //
    // Alt leads the cursor move and is released after the click, so a later op
    // in the same segment does not silently run with the modifier still held.
    void clickBelow(IRMath::ivec3 target) {
        if (m_eraseMode) {
            recordError(
                "clickBelow at local (" + std::to_string(target.x) + "," +
                std::to_string(target.y) + "," + std::to_string(target.z) +
                ") in erase mode: Alt inverts a PLACE normal, and erase acts on "
                "the hit voxel itself, so the editor would carve the anchor "
                "instead — in segment " +
                m_current.label_
            );
            return;
        }
        const std::optional<IRMath::vec3> aim = m_model.aimToPlaceBelow(target);
        if (!aim) {
            recordUnreachable("clickBelow", target);
            return;
        }
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kKeyButtonLeftAlt);
        // One idle frame before the aim, so Alt is already down when the editor
        // samples it at the button PRESS (chordKey's lead, for the same reason).
        m_frame += kFramesPerClickStep;
        emitClick(*aim);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kKeyButtonLeftAlt);
        m_model.setMirrored(target, true, m_symmetry);
    }

    // Park the cursor on `target`'s clickable face without pressing. Splits
    // "the aim is right" from "the gesture worked" — a hover segment's
    // expectPick says where the ray actually lands, so a failed edit doesn't
    // have to be diagnosed by guesswork.
    void hover(IRMath::ivec3 target) {
        const std::optional<IRMath::vec3> aim = aimFor(target);
        if (!aim) {
            recordUnreachable("hover", target);
            return;
        }
        emitMove(*aim);
    }

    // Left-drag box fill between two cells. In place mode `a` / `b` are the
    // cells to fill (each aimed via its own anchor face); in erase mode they
    // are existing voxels to carve.
    void dragBox(IRMath::ivec3 a, IRMath::ivec3 b) {
        if (pressBox("dragBox", a, b))
            releaseBox();
    }

    // dragBox up to and including its HELD frame: the button stays down with
    // the cursor on `b`, so the fill preview is still drawn at the segment's
    // capture. The harness carries the held button into the next segment,
    // where releaseBox commits the fill. A recipe that finishes with a box
    // held is an error.
    void holdBox(IRMath::ivec3 a, IRMath::ivec3 b) {
        pressBox("holdBox", a, b);
    }

    // Release the box holdBox is holding: the editor commits the fill and the
    // model follows.
    void releaseBox() {
        if (!m_heldBox) {
            recordError("releaseBox with no box held in segment " + m_current.label_);
            return;
        }
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
        m_model.setBoxMirrored(m_heldBox->a_, m_heldBox->b_, !m_eraseMode, m_symmetry);
        m_heldBox.reset();
    }

    // Tap a key with no modifier (V erase-mode toggle, X/Y/Z mirrors, K layer).
    void tapKey(IRInput::KeyMouseButtons key) {
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, key);
        m_cameraTurned =
            m_cameraTurned || key == IRInput::kKeyButtonQ || key == IRInput::kKeyButtonE;
    }

    // Tap a key while a modifier is held. The modifier leads the key by two
    // frames so it is already held when the key press drains.
    // Ctrl+Z and Ctrl+Y replay history the model does not keep, so after
    // either no part's recipe binding is known until it is APPLYed again.
    void chordKey(IRInput::KeyMouseButtons modifier, IRInput::KeyMouseButtons key) {
        const bool control = modifier == IRInput::kKeyButtonLeftControl ||
                             modifier == IRInput::kKeyButtonRightControl;
        if (control && (key == IRInput::kKeyButtonZ || key == IRInput::kKeyButtonY))
            forgetBindings();
        emitChord(modifier, key);
    }

  private:
    void emitChord(IRInput::KeyMouseButtons modifier, IRInput::KeyMouseButtons key) {
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, modifier);
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, modifier);
    }

  public:
    void toggleEraseMode() {
        tapKey(IRInput::kKeyButtonV);
        m_eraseMode = !m_eraseMode;
    }

    // G: gizmo handles take clicks only in transform mode, which starts off.
    // Enter it before a gizmo drag, and leave it before a paint click on a
    // cell a handle is drawn over, or the handle takes that click.
    void toggleTransformMode() {
        tapKey(IRInput::kKeyButtonG);
        m_transformMode = !m_transformMode;
    }

    // Enable mirror-symmetry axes for subsequent edits. Taps
    // the editor's X/Y/Z toggles so the live editor mirrors each edit, and turns
    // on the shadow model's mirroring against the same planes the editor uses:
    // the centre of the editable set's own extent, (size-1)/2, re-seated
    // whenever the editable set changes. Call once with every axis to enable;
    // assumes symmetry starts off (the editor's default).
    void enableSymmetry(bool x, bool y, bool z) {
        if (x) {
            tapKey(IRInput::kKeyButtonX);
            m_symmetry.enableX_ = true;
        }
        if (y) {
            tapKey(IRInput::kKeyButtonY);
            m_symmetry.enableY_ = true;
        }
        if (z) {
            tapKey(IRInput::kKeyButtonZ);
            m_symmetry.enableZ_ = true;
        }
        reseatMirrors();
    }

    // Create a new layer (K); the editor auto-activates it, so subsequent
    // placements land on it.
    void addLayer() {
        tapKey(IRInput::kKeyButtonK);
    }

    // Walk the active-layer selection back / forward through the layer list
    // ([ and ]). Both wrap, and neither moves voxels — only which layer the
    // next placement and the H visibility toggle apply to.
    void selectPrevLayer() {
        tapKey(IRInput::kKeyButtonLeftBracket);
    }

    void selectNextLayer() {
        tapKey(IRInput::kKeyButtonRightBracket);
    }

    // Toggle the active layer's visibility (H). A hidden layer's voxels report
    // unoccupied (alpha 0, the same liveness test evaluateOccupancyCheck reads),
    // so a hide-then-assert-empty / show-then-assert-full pair positively fires
    // the visibility path.
    void toggleActiveLayerVisibility() {
        tapKey(IRInput::kKeyButtonH);
    }

    // Click palette swatch @p index to make its colour the active paint colour.
    // The only op that aims at a GUI widget rather than the scene: the cursor
    // goes to the swatch's centre on the GUI canvas, so the click lands on the
    // widget's hitbox wherever the panel is laid out.
    void selectPaletteSwatch(int index) {
        if (index < 0 || index >= kPaletteCount) {
            recordError(
                "palette swatch " + std::to_string(index) + " out of range [0," +
                std::to_string(kPaletteCount) + ") in segment " + m_current.label_
            );
            return;
        }
        clickGui(paletteSwatchCenterGuiTrixel(index));
    }

    // Left click on a GUI-canvas point (a list row, a button).
    void clickGui(IRMath::vec2 guiTrixel) {
        emitGuiMove(guiTrixel);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
    }

    // Park the cursor on a GUI-canvas point and turn the mouse wheel @p rows
    // notches, one event a frame. Positive rows scroll a list toward its end
    // (wheel down); negative rows toward its start. Wheeling the RECIPES or
    // COMPONENTS list moves its shadow too, so a later named operation aims at
    // the row the live list shows.
    void wheelGui(IRMath::vec2 guiTrixel, int rows) {
        emitGuiMove(guiTrixel);
        emitWheel(rows);
        if (m_moduleView == nullptr)
            return;
        if (guiPointInRect(guiTrixel, kRecipeListPos, kRecipeListSize))
            m_recipeList.scrollBy(rows, kRecipeListSize.y);
        else if (guiPointInRect(guiTrixel, kComponentListPos, kComponentListSize))
            m_componentList.scrollBy(rows, kComponentListSize.y);
    }

    // Park the cursor on `target`'s clickable face and turn the wheel @p rows
    // notches, signed as wheelGui signs them.
    void wheelScene(IRMath::ivec3 target, int rows) {
        hover(target);
        emitWheel(rows);
    }

    // Click the PARTS list's @p visibleRow-th row from the top, which the
    // list's scroll offset currently maps to part @p partIndex. The shadow
    // model follows the selection as nextPart's does.
    void selectPartThroughList(int visibleRow, int partIndex) {
        if (partIndex < 0 || partIndex >= static_cast<int>(m_partModels.size()) || visibleRow < 0 ||
            visibleRow >= partsListVisibleRows()) {
            recordError(
                "selectPartThroughList row " + std::to_string(visibleRow) + " -> part " +
                std::to_string(partIndex) + " is out of range in segment " + m_current.label_
            );
            return;
        }
        m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        clickGui(partsListRowCenterGuiTrixel(visibleRow));
        m_activePart = partIndex;
        m_model = m_partModels[static_cast<std::size_t>(m_activePart)];
    }

    // Click the text input at `guiTrixel` to focus it, type `text` one key at
    // a time, and press Enter, which commits the text and drops the focus so
    // later key ops reach the editor's commands. `text` is limited to the
    // glyphs WIDGET_APPLY_TEXT_INPUT types.
    void typeText(IRMath::vec2 guiTrixel, std::string_view text) {
        clickGui(guiTrixel);
        for (char c : text) {
            const std::optional<TypedKey> key = typedKey(c);
            if (!key) {
                recordError(
                    std::string("typeText cannot type '") + c + "' in segment " + m_current.label_
                );
                return;
            }
            if (key->shift_)
                chordKey(IRInput::kKeyButtonLeftShift, key->key_);
            else
                tapKey(key->key_);
        }
        tapKey(IRInput::kKeyButtonEnter);
    }

    // Left-drag a GUI slider's track to `value`. PRESS lands at the track's
    // far end from the target, then a held MOVE slides to the target's own
    // trixel before RELEASE — the PRESS -> held MOVE -> RELEASE shape
    // system_widget_input.hpp's per-frame dragValue recompute needs to see a
    // real drag rather than a stationary click (a single move-then-click, the
    // way selectPaletteSwatch aims, would only ever land the value under the
    // cursor at press time). `minValue`/`maxValue` are passed separately from
    // `geom` because the scrubber's range tracks the live frame count, unlike
    // the fixed swatch grid selectPaletteSwatch aims from.
    void dragGuiSlider(
        const IRVoxelEditor::SliderGeometry &geom, float minValue, float maxValue, float value
    ) {
        if (m_lastDraggedSliderPos == geom.pos_ && m_segmentIndex - m_lastSliderDragSegment <= 1)
            m_frame += kSliderRepressIdleFrames;
        m_lastDraggedSliderPos = geom.pos_;
        m_lastSliderDragSegment = m_segmentIndex;
        const float farValue = (value > (minValue + maxValue) * 0.5f) ? minValue : maxValue;
        // GUI hitboxes are half-open ([pos, pos + size)), so the max end of the
        // track lies one trixel outside it: press one trixel inside either end.
        IRMath::vec2 press =
            IRVoxelEditor::sliderValueGuiTrixel(geom, minValue, maxValue, farValue);
        press.x = IRMath::clamp(
            press.x,
            static_cast<float>(geom.pos_.x) + 1.0f,
            static_cast<float>(geom.pos_.x + geom.size_.x) - 1.0f
        );
        emitGuiMove(press);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitGuiMove(IRVoxelEditor::sliderValueGuiTrixel(geom, minValue, maxValue, value));
        // One idle frame so the editor's HELD branch samples the moved cursor
        // before the release commits the value (dragBox's release timing).
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
    }

    void applyRecipe(
        const std::string &name, const std::vector<std::pair<std::string, float>> &requestedValues
    ) {
        if (m_moduleView == nullptr) {
            recordError("applyRecipe needs a loaded module");
            return;
        }
        const auto recipe = std::find_if(
            m_moduleView->recipes_.begin(),
            m_moduleView->recipes_.end(),
            [&name](const ModuleRecipe &candidate) { return candidate.name_ == name; }
        );
        if (recipe == m_moduleView->recipes_.end()) {
            recordError("applyRecipe: unknown recipe '" + name + "'");
            return;
        }
        std::vector<float> values;
        values.reserve(recipe->params_.size());
        for (const RecipeParam &param : recipe->params_) {
            const auto requested = std::find_if(
                requestedValues.begin(),
                requestedValues.end(),
                [&param](const auto &entry) { return entry.first == param.name_; }
            );
            const float value =
                requested == requestedValues.end() ? param.default_ : requested->second;
            if (param.type_ != RecipeParamType::INT) {
                recordError("applyRecipe: param '" + param.name_ + "' is not INT");
                return;
            }
            if (value < param.min_ || value > param.max_) {
                recordError("applyRecipe: param '" + param.name_ + "' is outside its range");
                return;
            }
            if (value != static_cast<float>(IRMath::roundHalfUp(value))) {
                recordError("applyRecipe: param '" + param.name_ + "' is not a whole number");
                return;
            }
            if ((param.max_ - param.min_) * 4.0f > static_cast<float>(kRecipeSliderSize.x)) {
                recordError(
                    "applyRecipe: param '" + param.name_ + "' has fewer than 4 trixels per unit"
                );
                return;
            }
            values.push_back(value);
        }
        for (const auto &requested : requestedValues) {
            const auto declared = std::find_if(
                recipe->params_.begin(),
                recipe->params_.end(),
                [&requested](const RecipeParam &param) { return param.name_ == requested.first; }
            );
            if (declared == recipe->params_.end()) {
                recordError("applyRecipe: unknown param '" + requested.first + "'");
                return;
            }
        }
        std::vector<RecipeCell> cells;
        std::string error;
        const std::size_t recipeIndex =
            static_cast<std::size_t>(recipe - m_moduleView->recipes_.begin());
        std::vector<RecipeClassMaskEntry> mask;
        if (m_activePart >= 0 && m_activePart < static_cast<int>(m_partBindings.size()) &&
            m_partBindings[static_cast<std::size_t>(m_activePart)].binding_ &&
            m_partBindings[static_cast<std::size_t>(m_activePart)].binding_->recipe_ == name) {
            mask = m_partBindings[static_cast<std::size_t>(m_activePart)].binding_->mask_;
        }
        if (!m_moduleView->evaluate_ ||
            !m_moduleView->evaluate_(recipeIndex, values, m_model.size(), mask, cells, error)) {
            recordError(
                "applyRecipe: " + (error.empty() ? std::string("evaluation failed") : error)
            );
            return;
        }
        clickGui(recipeListRowCenterGuiTrixel(revealListRow(
            m_recipeList,
            kRecipeListSize.y,
            recipeListRowCenterGuiTrixel(0),
            static_cast<int>(recipeIndex)
        )));
        const int lastPage = recipeParamPageCount(static_cast<int>(recipe->params_.size())) - 1;
        for (int page = 0; page < lastPage; ++page)
            clickGui(recipePageButtonCenterGuiTrixel(kRecipePagePrevPos));
        int page = 0;
        for (std::size_t i = 0; i < recipe->params_.size(); ++i) {
            const RecipeParam &param = recipe->params_[i];
            const int paramPage = static_cast<int>(i) / kRecipeParamRowsPerPage;
            while (page < paramPage) {
                clickGui(recipePageButtonCenterGuiTrixel(kRecipePageNextPos));
                ++page;
            }
            dragGuiSlider(
                recipeParamSliderGeometry(static_cast<int>(i) % kRecipeParamRowsPerPage),
                param.min_,
                param.max_,
                values[i]
            );
        }
        clickGui(recipeApplyCenterGuiTrixel());
        m_recipeListSelected = static_cast<int>(recipeIndex);
        for (const RecipeCell &cell : cells)
            m_model.set(cell.local_, true);
        if (m_activePart >= 0) {
            PartBinding &shadow = m_partBindings[static_cast<std::size_t>(m_activePart)];
            PartRecipeBinding binding;
            binding.recipe_ = name;
            binding.values_ = values;
            binding.fallbackColor_ = kPaletteColors[0];
            binding.mask_ = std::move(mask);
            // The mask APPLY keeps is only as certain as the shadow it came from,
            // and every mask is empty until the session paints a class.
            const bool maskKnown = shadow.known_ || recipe->classes_.empty() || !m_classPaintMode;
            shadow = PartBinding{std::move(binding), maskKnown};
        }
    }

    void paintClass(IRMath::ivec3 local, const std::string &className) {
        if (m_moduleView == nullptr || m_activePart < 0 ||
            m_activePart >= static_cast<int>(m_partBindings.size()) ||
            !m_partBindings[static_cast<std::size_t>(m_activePart)].binding_) {
            recordError("paintClass needs a recipe-bound selected part");
            return;
        }
        if (!m_partBindings[static_cast<std::size_t>(m_activePart)].known_) {
            recordError(
                "paintClass: an undo, redo or reload may have changed the selected part's "
                "binding; APPLY its recipe again first"
            );
            return;
        }
        PartRecipeBinding &binding =
            *m_partBindings[static_cast<std::size_t>(m_activePart)].binding_;
        const auto recipe = std::find_if(
            m_moduleView->recipes_.begin(),
            m_moduleView->recipes_.end(),
            [&](const ModuleRecipe &candidate) { return candidate.name_ == binding.recipe_; }
        );
        if (recipe == m_moduleView->recipes_.end()) {
            recordError("paintClass: bound recipe is not registered");
            return;
        }
        const auto declared =
            std::find(recipe->classes_.begin(), recipe->classes_.end(), className);
        if (declared == recipe->classes_.end()) {
            recordError("paintClass: unknown class '" + className + "'");
            return;
        }
        const std::size_t recipeIndex =
            static_cast<std::size_t>(recipe - m_moduleView->recipes_.begin());
        std::vector<RecipeCell> oldCells;
        std::vector<RecipeCell> newCells;
        std::string error;
        if (!m_moduleView->evaluate_(
                recipeIndex,
                binding.values_,
                m_model.size(),
                binding.mask_,
                oldCells,
                error
            )) {
            recordError("paintClass: " + error);
            return;
        }
        PartRecipeBinding updatedBinding = binding;
        auto entry = std::find_if(
            updatedBinding.mask_.begin(),
            updatedBinding.mask_.end(),
            [&](const auto &item) { return item.local_ == local; }
        );
        if (entry == updatedBinding.mask_.end())
            updatedBinding.mask_.push_back({local, className});
        else
            entry->className_ = className;
        if (!m_moduleView->evaluate_(
                recipeIndex,
                updatedBinding.values_,
                m_model.size(),
                updatedBinding.mask_,
                newCells,
                error
            )) {
            recordError("paintClass: " + error);
            return;
        }
        const std::optional<IRMath::vec3> aim = m_model.aimAtVoxel(local);
        if (!aim) {
            recordUnreachable("paintClass", local);
            return;
        }
        binding = std::move(updatedBinding);
        for (const RecipeCell &cell : oldCells) {
            const bool remains =
                std::any_of(newCells.begin(), newCells.end(), [&](const auto &item) {
                    return item.local_ == cell.local_;
                });
            if (!remains)
                m_model.set(cell.local_, false);
        }
        for (const RecipeCell &cell : newCells)
            m_model.set(cell.local_, true);
        selectPaletteSwatch(static_cast<int>(declared - recipe->classes_.begin()));
        if (!m_classPaintMode) {
            tapKey(IRInput::kKeyButtonU);
            m_classPaintMode = true;
        }
        emitClick(*aim);
    }

    // Drag the selected part's recipe handle @p handle to set-local cell
    // @p target: press on the handle's `at` point, move to @p target, release.
    // The target must lie on the screen plane through `at` (the same world
    // coordinate sum once both turn with the part, which on an unturned part
    // is the same set-local sum), the plane the editor reads the drag on. The
    // model takes the handle's `drag` result at the press cell and then at
    // @p target, replacing the recipe's cells as the editor does, and the
    // RECIPES scroll shadow follows the list's re-pointing. A recipe error,
    // with nothing emitted: outside transform mode, with the camera turned in
    // this segment, on a turned GRID part (the model knows a turn from an
    // array's COPY YAW and a mode from selectPartMode), with no binding on
    // the selected part (or one an undo, redo, clear or reload may have
    // changed), an unknown handle, or `at` / `drag` raising.
    // A handle press lands on whatever owns the pixel under `at`: start from
    // a handle the part's gizmo leaves uncovered.
    void dragRecipeHandle(const std::string &handle, IRMath::ivec3 target) {
        const std::string op = "dragRecipeHandle('" + handle + "')";
        if (m_moduleView == nullptr) {
            recordError(op + " needs a loaded module");
            return;
        }
        if (m_heldBox || m_heldGizmo) {
            recordError(op + " while a button is held in segment " + m_current.label_);
            return;
        }
        if (!m_transformMode) {
            recordError(op + " outside transform mode, where no recipe handle is drawn");
            return;
        }
        if (m_cameraTurned) {
            recordError(op + " with the camera turned in segment " + m_current.label_);
            return;
        }
        if (m_activePart < 0) {
            recordError(op + " needs an entity-scene part");
            return;
        }
        if (!m_model.cellsEditableInPlace()) {
            recordError(op + " on a turned GRID part, which draws no recipe handle");
            return;
        }
        PartBinding &shadow = m_partBindings[static_cast<std::size_t>(m_activePart)];
        if (!shadow.known_ || !shadow.binding_) {
            recordError(
                op + (shadow.known_
                          ? ": the selected part has no recipe binding"
                          : ": an undo, redo or reload may have changed the selected part's "
                            "binding; APPLY its recipe again first")
            );
            return;
        }
        const PartRecipeBinding &binding = *shadow.binding_;
        const auto bound = std::find_if(
            m_moduleView->recipes_.begin(),
            m_moduleView->recipes_.end(),
            [&](const ModuleRecipe &candidate) { return candidate.name_ == binding.recipe_; }
        );
        if (bound == m_moduleView->recipes_.end()) {
            recordError(op + ": the bound recipe is not registered");
            return;
        }
        const ModuleRecipe &recipe = *bound;
        const std::size_t recipeIndex =
            static_cast<std::size_t>(bound - m_moduleView->recipes_.begin());
        const auto declared =
            std::find_if(recipe.handles_.begin(), recipe.handles_.end(), [&](const auto &h) {
                return h.name_ == handle;
            });
        if (declared == recipe.handles_.end()) {
            recordError(op + ": recipe '" + recipe.name_ + "' declares no such handle");
            return;
        }
        const std::size_t handleIndex =
            static_cast<std::size_t>(declared - recipe.handles_.begin());
        IRMath::vec3 at(0.0f);
        std::string error;
        if (!m_moduleView->handleAt_ || !m_moduleView->handleDrag_ ||
            !m_moduleView->handleAt_(
                recipeIndex,
                handleIndex,
                binding.values_,
                m_model.size(),
                at,
                error
            )) {
            recordError(op + ": " + error);
            return;
        }
        const IRMath::vec3 worldAt = m_model.worldPoint(at);
        const IRMath::vec3 worldTarget = m_model.worldPoint(IRMath::vec3(target));
        if (IRMath::abs(
                worldAt.x + worldAt.y + worldAt.z - (worldTarget.x + worldTarget.y + worldTarget.z)
            ) > kRecipeHandlePlaneSlack) {
            recordError(
                op + ": the target's world coordinate sum differs from at's, so it is off the "
                     "plane the drag is read on"
            );
            return;
        }
        // The editor rounds the cursor's plane point, which lands within a
        // pixel of `at`; a component near a half cell could round either way.
        const IRMath::ivec3 pressCell = IRMath::roundVec3HalfUp(at);
        const IRMath::vec3 pressOffset = IRMath::abs(at - IRMath::vec3(pressCell));
        if (IRMath::max(pressOffset.x, IRMath::max(pressOffset.y, pressOffset.z)) >
            kRecipeHandlePressSlack) {
            recordError(op + ": at is too close to a half cell to name the press cell");
            return;
        }
        OccupancyModel model = m_model;
        std::vector<float> values = binding.values_;
        for (const IRMath::ivec3 &point : {pressCell, target}) {
            std::vector<float> dragged;
            if (!m_moduleView->handleDrag_(
                    recipeIndex,
                    handleIndex,
                    binding.values_,
                    m_model.size(),
                    point,
                    dragged,
                    error
                ) ||
                (dragged != values &&
                 !replaceRecipeCells(model, recipeIndex, binding.mask_, values, dragged, error))) {
                recordError(op + ": " + error);
                return;
            }
            values = std::move(dragged);
        }
        emitMove(worldAt);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitMove(worldTarget);
        // One idle frame so the drag reads the moved cursor before the release.
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
        if (values != binding.values_ && m_recipeListSelected != static_cast<int>(recipeIndex)) {
            m_recipeList.scrollIntoView(static_cast<int>(recipeIndex), kRecipeListSize.y);
            m_recipeListSelected = static_cast<int>(recipeIndex);
        }
        m_model = std::move(model);
        shadow.binding_->values_ = std::move(values);
    }

    void attachComponent(const std::string &name) {
        const ComponentRecord *component = findComponent(name, "attachComponent");
        if (component == nullptr)
            return;
        clickComponentRow(component);
        clickGui(componentButtonCenterGuiTrixel(kComponentAttachPos));
    }

    void setComponentField(
        const std::string &componentName, const std::string &fieldName, std::string_view text
    ) {
        const ComponentRecord *component = findComponent(componentName, "setComponentField");
        if (component == nullptr)
            return;
        if (component->source_ != ComponentSource::MODULE) {
            recordError(
                "setComponentField: component '" + componentName + "' is not a module component"
            );
            return;
        }
        const auto field = std::find_if(
            component->fields_.begin(),
            component->fields_.end(),
            [&fieldName](const ComponentField &candidate) { return candidate.name_ == fieldName; }
        );
        if (field == component->fields_.end()) {
            recordError("setComponentField: unknown field '" + fieldName + "'");
            return;
        }
        if (!parseFieldText(field->type_, text)) {
            recordError(
                "setComponentField: field '" + fieldName + "' cannot hold '" + std::string(text) +
                "'"
            );
            return;
        }
        const int fieldIndex = static_cast<int>(field - component->fields_.begin());
        const int fieldPage = fieldIndex / kComponentFieldRowsPerPage;
        const int lastPage =
            componentFieldPageCount(static_cast<int>(component->fields_.size())) - 1;
        clickComponentRow(component);
        for (int page = 0; page < lastPage; ++page)
            clickGui(componentButtonCenterGuiTrixel(kComponentPagePrevPos));
        for (int page = 0; page < fieldPage; ++page)
            clickGui(componentButtonCenterGuiTrixel(kComponentPageNextPos));
        typeText(componentFieldInputCenterGuiTrixel(fieldIndex % kComponentFieldRowsPerPage), text);
    }

    void setComponentOverrides(const std::string &componentName, std::string_view text) {
        const ComponentRecord *component = findComponent(componentName, "setComponentOverrides");
        if (component == nullptr)
            return;
        if (component->source_ != ComponentSource::ENGINE) {
            recordError(
                "setComponentOverrides: component '" + componentName +
                "' is not an engine component"
            );
            return;
        }
        clickComponentRow(component);
        typeText(componentOverridesInputCenterGuiTrixel(), text);
    }

    // Duplicate the active animation frame (D) and select the copy. The editor
    // snapshots the live voxels into the source frame first, so the duplicate
    // starts as an exact copy — which is why the shadow model can simply be
    // cloned rather than re-derived.
    //
    // D is also the camera-right binding (no modifier guard), so the
    // caller must open a new segment afterwards to re-apply the camera before
    // aiming anything.
    void duplicateFrame() {
        tapKey(IRInput::kKeyButtonD);
        m_frameModels.insert(m_frameModels.begin() + m_activeFrame, m_model);
        ++m_activeFrame;
    }

    // Step the active frame back / forward (Left / Right arrow). The editor
    // snapshots the live voxels into the departing frame and loads the arriving
    // one; the shadow model follows the same swap, so aiming stays valid on
    // either side of a step (unlike reload(), which repopulates from disk).
    // Stepping past either end is a no-op in the editor (switchToFrame clamps),
    // so the recipe is rejected rather than silently diverging from the live
    // set.
    void prevFrame() {
        stepFrame(-1);
    }

    void nextFrame() {
        stepFrame(1);
    }

    void save() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonS);
    }

    // Reload the last-saved scene from disk (Ctrl+O) — the save→load round-trip
    // check. The shadow model is NOT re-synced from disk, so issue no aiming ops
    // after this; only occupancy asserts, which read the live reloaded set.
    void reload() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonO);
        forgetBindings();
    }

    // Drag the PART SIZE sliders to @p size: the extent every later
    // addVoxelPart creates, until the next call. Each axis must lie on its
    // track (part_size_panel.hpp).
    void setPartSize(IRMath::ivec3 size) {
        for (int axis = 0; axis < kPartSizeAxisCount; ++axis) {
            const float maxValue = partSizeSliderMax(m_sceneSize[axis]);
            if (size[axis] < static_cast<int>(kPartSizeSliderMin) ||
                size[axis] > static_cast<int>(maxValue)) {
                recordError(
                    "setPartSize axis " + std::to_string(axis) + " = " +
                    std::to_string(size[axis]) + " is off the slider track in segment " +
                    m_current.label_
                );
                return;
            }
            dragGuiSlider(
                partSizeSliderGeometry(axis),
                kPartSizeSliderMin,
                maxValue,
                static_cast<float>(size[axis])
            );
        }
        m_partSize = size;
    }

    // Ctrl+P: add a voxel part at the PART SIZE sliders' extent and select it.
    // The editor seats the part's cell (0,0,0) at deriveSceneOrigin, which is
    // the model's origin too.
    void addVoxelPart() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonP);
        if (!m_partModels.empty()) {
            m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        }
        OccupancyModel added(m_partSize, deriveSceneOrigin(m_partSize));
        m_partModels.push_back(added);
        m_partBindings.emplace_back();
        m_activePart = static_cast<int>(m_partModels.size()) - 1;
        m_model = std::move(added);
        reseatMirrors();
    }

    // Sets the BAKE panel's P1 slider to @p radius and clicks BAKE with the
    // default shape row (a sphere): the first write into a part that has no
    // voxel for a click to pick. Open a new segment before aiming at the
    // result.
    //
    // The editor bakes at the radius the slider holds, which the drag leaves
    // within kBakeParamSliderDragDrift of @p radius. A sphere only gains cells
    // as its radius grows, so the bakes at the two ends of that range bracket
    // every bake the editor can run: when both leave the same occupancy, that
    // occupancy is the model. A radius off the track, or one whose range spans
    // a change in the baked cells, is a recipe error that emits no gesture and
    // leaves the model as it was.
    void bakeSphere(float radius) {
        const float minRadius = IRVoxelEditor::kBakeParamSliderMin;
        const float maxRadius = IRVoxelEditor::kBakeParamSliderMax;
        // Written so a NaN fails it.
        if (!(radius >= minRadius && radius <= maxRadius)) {
            recordError(
                "bakeSphere radius " + std::to_string(radius) +
                " is off the BAKE slider's track [" + std::to_string(minRadius) + ", " +
                std::to_string(maxRadius) + "] in segment " + m_current.label_
            );
            return;
        }
        const float heldLow =
            IRMath::max(radius - IRVoxelEditor::kBakeParamSliderDragDrift, minRadius);
        const float heldHigh =
            IRMath::min(radius + IRVoxelEditor::kBakeParamSliderDragDrift, maxRadius);
        OccupancyModel baked = sphereBakedAt(heldLow);
        if (!baked.sameOccupancy(sphereBakedAt(heldHigh))) {
            recordError(
                "bakeSphere radius " + std::to_string(radius) + " is ambiguous in segment " +
                m_current.label_ + ": the BAKE slider can hold anything in [" +
                std::to_string(heldLow) + ", " + std::to_string(heldHigh) +
                "] and the baked cells change inside that range"
            );
            return;
        }
        dragGuiSlider(IRVoxelEditor::kBakeParam1SliderGeometry, minRadius, maxRadius, radius);
        clickGui(IRVoxelEditor::bakeButtonCenterGuiTrixel());
        m_model = std::move(baked);
    }

    void nextPart() {
        if (m_partModels.empty()) {
            recordError("nextPart needs an entity-scene part");
            return;
        }
        m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        tapKey(IRInput::kKeyButtonTab);
        m_activePart = (m_activePart + 1) % static_cast<int>(m_partModels.size());
        m_model = m_partModels[static_cast<std::size_t>(m_activePart)];
        reseatMirrors();
    }

    // F: toggle loft mode. While it is on the scene takes no edit click.
    void toggleLoftMode() {
        tapKey(IRInput::kKeyButtonF);
    }

    // Click loft-mask cell @p cell (h, v), toggling it. Needs loft mode on.
    void clickLoftCell(LoftMask mask, IRMath::ivec2 cell) {
        const bool front = mask == LoftMask::XZ;
        const int columns = front ? m_sceneSize.x : m_sceneSize.y;
        // The XZ grid is hit-tested first, so a YZ click it also covers would
        // land in the XZ mask.
        const bool underFrontGrid = !front && kLoftGridYZPos.x + cell.x * kLoftCellPx <
                                                  kLoftGridXZPos.x + m_sceneSize.x * kLoftCellPx;
        if (cell.x < 0 || cell.x >= columns || cell.y < 0 || cell.y >= m_sceneSize.z ||
            underFrontGrid) {
            recordError(
                "clickLoftCell (" + std::to_string(cell.x) + "," + std::to_string(cell.y) +
                ") is not a clickable cell of that mask in segment " + m_current.label_
            );
            return;
        }
        clickGui(
            loftCellCenterGuiTrixel(front ? kLoftGridXZPos : kLoftGridYZPos, cell, m_sceneSize.z)
        );
    }

    // Enter: stamp the loft masks into the editable set. Needs loft mode on.
    void stampLoft() {
        tapKey(IRInput::kKeyButtonEnter);
    }

    void removeSelectedPart() {
        if (m_partModels.empty() || m_activePart < 0) {
            recordError("removeSelectedPart needs an entity-scene part");
            return;
        }
        tapKey(IRInput::kKeyButtonDelete);
        recordSelectedPartRemoval();
    }

    void removeSelectedPartWithButton() {
        if (m_partModels.empty() || m_activePart < 0) {
            recordError("removeSelectedPartWithButton needs an entity-scene part");
            return;
        }
        clickGui(IRVoxelEditor::partRemoveButtonCenterGuiTrixel());
        recordSelectedPartRemoval();
    }

    void undoRemovedPart() {
        if (!m_removedPartModel) {
            recordError("undoRemovedPart needs a preceding part removal");
            return;
        }
        emitChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonZ);
        const int index = m_removedPartModel->index_;
        m_partModels.insert(m_partModels.begin() + index, std::move(m_removedPartModel->model_));
        m_partBindings.insert(
            m_partBindings.begin() + index,
            std::move(m_removedPartModel->binding_)
        );
        m_activePart = index;
        m_model = m_partModels[static_cast<std::size_t>(m_activePart)];
        m_removedPartModel.reset();
    }

    void discardRemovedPart() {
        m_removedPartModel.reset();
        m_partModels.clear();
        m_partBindings.clear();
        m_activePart = -1;
    }

    // Drag one LOD panel slider to tier @p tier (0 finest .. 4 coarsest).
    void dragLodSlider(SliderTarget target, int tier) {
        const IRVoxelEditor::SliderGeometry *geom = nullptr;
        switch (target) {
        case SliderTarget::LOD_FINE:
            geom = &IRVoxelEditor::kLodFineSliderGeometry;
            break;
        case SliderTarget::LOD_COARSE:
            geom = &IRVoxelEditor::kLodCoarseSliderGeometry;
            break;
        case SliderTarget::LOD_TIER:
            geom = &IRVoxelEditor::kLodTierSliderGeometry;
            break;
        case SliderTarget::FPS:
        case SliderTarget::SCRUBBER:
            recordError("dragLodSlider needs a LOD panel slider in segment " + m_current.label_);
            return;
        }
        dragGuiSlider(
            *geom,
            IRVoxelEditor::kLodTierSliderMin,
            IRVoxelEditor::kLodTierSliderMax,
            static_cast<float>(tier)
        );
    }

    void toggleLodFollowZoom() {
        clickGui(IRVoxelEditor::lodFollowCheckboxCenterGuiTrixel());
    }

    void clearEntityScene() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonBackspace);
        forgetBindings();
    }

    void dragWorld(IRMath::vec3 from, IRMath::vec3 to) {
        emitMove(from);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitMove(to);
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
    }

    // Left-drag the selected part's move gizmo from @p from to @p to, which
    // translates the part by @p partDelta; the model follows, so later aims
    // land on the moved part. The gizmo is drawn at the part's pivot, the
    // middle of its box, in front of the part's own content. A handle is a
    // few trixels across at the session zoom, and which of its pixels are
    // drawn depends on where the part sits on the voxel lattice and on the
    // camera's yaw: take a new grab point from a run, not from the handle's
    // geometry.
    void dragPartGizmo(IRMath::vec3 from, IRMath::vec3 to, IRMath::vec3 partDelta) {
        if (pressPartGizmo("dragPartGizmo", from, to, partDelta))
            releasePartGizmo();
    }

    // dragPartGizmo up to its last move: the button stays down on @p to, so the
    // drag is still active at the segment's capture, and releasePartGizmo ends
    // it in a later segment. The model takes @p partDelta at once: a drag that
    // ends early (transform mode turned off under it) keeps the moved part. A
    // recipe that finishes with a gizmo held is an error.
    void holdPartGizmo(IRMath::vec3 from, IRMath::vec3 to, IRMath::vec3 partDelta) {
        pressPartGizmo("holdPartGizmo", from, to, partDelta);
    }

    void releasePartGizmo() {
        if (!m_heldGizmo) {
            recordError("releasePartGizmo with no gizmo held in segment " + m_current.label_);
            return;
        }
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
        m_heldGizmo = false;
    }

    // Pick @p mode in the PART MODE dropdown: one click opens it, the next
    // lands on the mode's row. The idle frame lets the opened dropdown grow
    // its hit region before the cursor moves onto a row.
    void selectPartMode(IRComponents::RotationMode mode) {
        clickGui(IRVoxelEditor::partModeDropdownHeaderCenterGuiTrixel());
        m_frame += kFramesPerClickStep;
        clickGui(IRVoxelEditor::partModeDropdownRowCenterGuiTrixel(mode));
        m_model.setMode(mode);
    }

    // Assert the live editable set's occupancy at `local` when this segment
    // settles — i.e. after every event in the segment has fired, not at this
    // call's position in the op sequence (see segment()).
    void expectOccupancy(IRMath::ivec3 local, bool expectOccupied, std::string name) {
        addOccupancyExpectation(-1, local, expectOccupied, std::move(name));
    }

    void
    expectPartOccupancy(int partIndex, IRMath::ivec3 local, bool expectOccupied, std::string name) {
        addOccupancyExpectation(partIndex, local, expectOccupied, std::move(name));
    }

    void expectPartCellClass(
        int partIndex, IRMath::ivec3 local, std::optional<std::string> expected, std::string name
    ) {
        addPredicateCheck(
            m_recipe.partClassMaskChecks_,
            PartClassMaskCheck{partIndex, local, std::move(expected), std::move(name)},
            &evaluatePartClassMaskCheck
        );
    }

    void expectPartTransform(
        int partIndex, IRMath::vec3 expected, float tolerance, bool expectEqual, std::string name
    ) {
        addPredicateCheck(
            m_recipe.partTransformChecks_,
            PartTransformCheck{partIndex, expected, tolerance, expectEqual, std::move(name)},
            &evaluatePartTransformCheck
        );
    }

    void expectPartCount(int expected, std::string name) {
        addPredicateCheck(
            m_recipe.partCountChecks_,
            PartCountCheck{expected, std::move(name)},
            &evaluatePartCountCheck
        );
    }

    void expectPartId(int partIndex, std::string expected, std::string name) {
        addPredicateCheck(
            m_recipe.partIdChecks_,
            PartIdCheck{partIndex, std::move(expected), std::move(name)},
            &evaluatePartIdCheck
        );
    }

    void expectPartAuthoredCount(int partIndex, int expected, std::string name) {
        addPredicateCheck(
            m_recipe.partAuthoredCountChecks_,
            PartAuthoredCountCheck{partIndex, expected, std::move(name)},
            &evaluatePartAuthoredCountCheck
        );
    }

    void expectPartEditable(int partIndex, bool expected, std::string name) {
        addPredicateCheck(
            m_recipe.partEditableChecks_,
            PartEditableCheck{partIndex, expected, std::move(name)},
            &evaluatePartEditableCheck
        );
    }

    // Assert the PARTS list and the entity scene both select part
    // @p partIndex and the list paints that row.
    void expectSelectedPartVisible(int partIndex, std::string name) {
        addPredicateCheck(
            m_recipe.partsListChecks_,
            PartsListCheck{partIndex, -1, std::move(name)},
            &evaluatePartsListCheck
        );
    }

    // Assert part @p partIndex is the PARTS list's first visible item.
    void expectPartsListTop(int partIndex, std::string name) {
        addPredicateCheck(
            m_recipe.partsListChecks_,
            PartsListCheck{-1, partIndex, std::move(name)},
            &evaluatePartsListCheck
        );
    }

    // Assert the live camera zoom equals @p zoom when this segment settles,
    // or with @p expectEqual false, that it does not.
    void expectCameraZoom(float zoom, bool expectEqual, std::string name) {
        addPredicateCheck(
            m_recipe.cameraZoomChecks_,
            CameraZoomCheck{zoom, expectEqual, std::move(name)},
            &evaluateCameraZoomCheck
        );
    }

    void applyRadialArray(int count) {
        applyRadialArray(count, kArrayDefaultRadius, 0);
    }

    void applyRadialArray(int count, int radius) {
        applyRadialArray(count, radius, 0);
    }

    void applyRadialArray(int count, int radius, int copyYawDegrees) {
        if (!configureRadialArray(count, radius, copyYawDegrees))
            return;
        if (m_partModels.empty()) {
            recordError("radial array needs an entity-scene part");
            return;
        }
        m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        const OccupancyModel source = m_model;
        const auto offsets = IRPrefab::Arrays::radial(
            count,
            IRMath::vec3(0.0f, 0.0f, 1.0f),
            static_cast<float>(radius),
            static_cast<float>(copyYawDegrees) * IRMath::kPi / 180.0f
        );
        for (const auto &offset : offsets) {
            OccupancyModel copy = source;
            copy.setOrigin(source.origin() + offset.translation_);
            copy.setRotation(IRMath::quatMul(offset.rotation_, source.rotation()));
            m_partModels.push_back(std::move(copy));
            m_partBindings.push_back(m_partBindings[static_cast<std::size_t>(m_activePart)]);
        }
        m_activePart = static_cast<int>(m_partModels.size()) - 1;
        m_model = m_partModels.back();
        reseatMirrors();
    }

    void applyRadialArrayToLiveScene(int count) {
        applyRadialArrayToLiveScene(count, kArrayDefaultRadius, 0);
    }

    void applyRadialArrayToLiveScene(int count, int radius) {
        applyRadialArrayToLiveScene(count, radius, 0);
    }

    void applyRadialArrayToLiveScene(int count, int radius, int copyYawDegrees) {
        configureRadialArray(count, radius, copyYawDegrees);
    }

  private:
    bool configureRadialArray(int count, int radius, int copyYawDegrees) {
        if (radius < static_cast<int>(kArrayRadiusSliderMin) ||
            radius > static_cast<int>(kArrayRadiusSliderMax)) {
            recordError("radial array radius is outside the RADIUS/X range");
            return false;
        }
        if (copyYawDegrees < static_cast<int>(kArrayYawSliderMin) ||
            copyYawDegrees > static_cast<int>(kArrayYawSliderMax) ||
            copyYawDegrees % static_cast<int>(kArrayYawDegreesPerTrixel) != 0) {
            recordError("radial array yaw must be a reachable COPY YAW value");
            return false;
        }
        dragGuiSlider(kArrayCountSliderGeometry, 2.0f, 12.0f, static_cast<float>(count));
        dragGuiSlider(
            kArrayRadiusSliderGeometry,
            kArrayRadiusSliderMin,
            kArrayRadiusSliderMax,
            static_cast<float>(radius)
        );
        dragGuiSlider(
            kArrayYawSliderGeometry,
            kArrayYawSliderMin,
            kArrayYawSliderMax,
            static_cast<float>(copyYawDegrees)
        );
        clickGui(kArrayApplyCenter);
        return true;
    }

  public:
    void toggleRotationalSymmetry() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonY);
    }

    void expectCanvasCount(int expectedOffset, std::string name) {
        addPredicateCheck(
            m_recipe.canvasCountChecks_,
            CanvasCountCheck{expectedOffset, std::move(name)},
            &evaluateCanvasCountCheck
        );
    }

    void expectRotationMode(int partIndex, IRComponents::RotationMode expected, std::string name) {
        addPredicateCheck(
            m_recipe.rotationModeChecks_,
            RotationModeCheck{partIndex, expected, std::move(name)},
            &evaluateRotationModeCheck
        );
    }

    // Assert part @p partIndex's voxel anchor when this segment settles.
    void expectPartAnchor(int partIndex, IRComponents::EntityAnchor expected, std::string name) {
        addPredicateCheck(
            m_recipe.partAnchorChecks_,
            PartAnchorCheck{partIndex, expected, std::move(name)},
            &evaluatePartAnchorCheck
        );
    }

    // Assert part @p partIndex is turned @p degrees about +z, to within
    // @p toleranceDegrees on the circle, when this segment settles.
    void expectPartYaw(int partIndex, float degrees, float toleranceDegrees, std::string name) {
        addPredicateCheck(
            m_recipe.partYawChecks_,
            PartYawCheck{partIndex, degrees, toleranceDegrees, std::move(name)},
            &evaluatePartYawCheck
        );
    }

    // Assert part @p partIndex is turned at least @p minDegrees about plus or
    // minus @p axis, a unit vector in the part's own frame, when this segment
    // settles. A turn composed onto an earlier one is about no single axis:
    // start the gesture on an unturned part.
    void expectPartTurnAxis(int partIndex, IRMath::vec3 axis, float minDegrees, std::string name) {
        addPredicateCheck(
            m_recipe.partTurnAxisChecks_,
            PartTurnAxisCheck{partIndex, axis, minDegrees, std::move(name)},
            &evaluatePartTurnAxisCheck
        );
    }

    // Assert GIZMO_DRAG is (or, with @p expectActive false, is not) driving a
    // drag when this segment settles: the positive fire for holdPartGizmo, and
    // for a gesture that must end one.
    void expectGizmoDragActive(bool expectActive, std::string name) {
        addPredicateCheck(
            m_recipe.gizmoDragChecks_,
            GizmoDragCheck{expectActive, std::move(name)},
            &evaluateGizmoDragCheck
        );
    }

    // Record the main-canvas texels gizmo handles are drawn on when this
    // segment settles; fails when there are none.
    // expectGizmoHandleTexelsUnchanged compares a later segment against it.
    void expectGizmoHandleTexels(std::string name) {
        addPredicateCheck(
            m_recipe.gizmoTexelChecks_,
            GizmoTexelCheck{nullptr, {}, std::move(name)},
            &evaluateGizmoTexelCheck
        );
        m_lastGizmoTexels = &m_recipe.gizmoTexelChecks_.back();
    }

    // Assert gizmo handles are drawn on exactly the texels the last
    // expectGizmoHandleTexels recorded.
    void expectGizmoHandleTexelsUnchanged(std::string name) {
        if (m_lastGizmoTexels == nullptr) {
            recordError(
                "expectGizmoHandleTexelsUnchanged with no expectGizmoHandleTexels before it "
                "in segment " +
                m_current.label_
            );
            return;
        }
        addPredicateCheck(
            m_recipe.gizmoTexelChecks_,
            GizmoTexelCheck{m_lastGizmoTexels, {}, std::move(name)},
            &evaluateGizmoTexelCheck
        );
    }

    // Assert part @p partIndex's live binding holds @p value for @p param.
    void expectRecipeValue(int partIndex, std::string param, float value, std::string name) {
        addPredicateCheck(
            m_recipe.recipeValueChecks_,
            RecipeValueCheck{partIndex, std::move(param), value, std::move(name)},
            &evaluateRecipeValueCheck
        );
    }

    // Assert part @p partIndex's filled cells are exactly its bound recipe's
    // cells at its bound values; a failure names the first differing cell.
    void expectPartMatchesRecipe(int partIndex, std::string name) {
        addPredicateCheck(
            m_recipe.partRecipeMatchChecks_,
            PartRecipeMatchCheck{partIndex, std::move(name)},
            &evaluatePartRecipeMatchCheck
        );
    }

    // Assert the selected part's recipe handle @p handle owns at least one
    // main-canvas texel, or with @p drawn false, none.
    void expectRecipeHandleTexels(std::string handle, bool drawn, std::string name) {
        addPredicateCheck(
            m_recipe.recipeHandleTexelChecks_,
            RecipeHandleTexelCheck{std::move(handle), drawn, std::move(name)},
            &evaluateRecipeHandleTexelCheck
        );
    }

    // Assert part @p partIndex carries a C_AutoSpin turning @p radiansPerFrame
    // a tick when this segment settles, or with @p expectEqual false, that it
    // does not.
    void
    expectPartSpinRate(int partIndex, float radiansPerFrame, bool expectEqual, std::string name) {
        addPredicateCheck(
            m_recipe.partSpinChecks_,
            PartSpinCheck{partIndex, radiansPerFrame, expectEqual, std::move(name)},
            &evaluatePartSpinCheck
        );
    }

    // Assert the PART MODE dropdown is collapsed on @p expected's row when
    // this segment settles.
    void expectPartModeDropdown(IRComponents::RotationMode expected, std::string name) {
        addPredicateCheck(
            m_recipe.partModeDropdownChecks_,
            PartModeDropdownCheck{expected, std::move(name)},
            &evaluatePartModeDropdownCheck
        );
    }

    void expectEditorInput(EditorInputCheckKind kind, int expectedPart, std::string name) {
        addPredicateCheck(
            m_recipe.editorInputChecks_,
            EditorInputCheck{kind, expectedPart, std::move(name)},
            &evaluateEditorInputCheck
        );
    }

  private:
    const ComponentRecord *findComponent(const std::string &name, const char *operation) {
        if (m_moduleView == nullptr) {
            recordError(std::string(operation) + " needs a loaded module");
            return nullptr;
        }
        const auto component = std::find_if(
            m_moduleView->components_.begin(),
            m_moduleView->components_.end(),
            [&name](const ComponentRecord &candidate) { return candidate.name_ == name; }
        );
        if (component == m_moduleView->components_.end()) {
            recordError(std::string(operation) + ": unknown component '" + name + "'");
            return nullptr;
        }
        return &*component;
    }

    // Wheel @p list, whose view is @p viewHeight tall, until item @p index is
    // on screen, with the live widget's least movement and per-notch clamp,
    // and return the visible row it lands on. A visible item turns no wheel.
    // The shadow moves only through wheelGui, so @p wheelPoint must lie on
    // @p list.
    int revealListRow(
        IRComponents::C_WidgetList &list, int viewHeight, IRMath::vec2 wheelPoint, int index
    ) {
        const int top = list.topIndex(viewHeight);
        list.scrollIntoView(index, viewHeight);
        const int rows = list.topIndex(viewHeight) - top;
        list.scrollOffset_ = top;
        if (rows != 0)
            wheelGui(wheelPoint, rows);
        return index - list.topIndex(viewHeight);
    }

    void clickComponentRow(const ComponentRecord *component) {
        const int index = static_cast<int>(component - m_moduleView->components_.data());
        clickGui(componentListRowCenterGuiTrixel(revealListRow(
            m_componentList,
            kComponentListSize.y,
            componentListRowCenterGuiTrixel(0),
            index
        )));
    }

    template <typename Check>
    void addPredicateCheck(
        std::deque<Check> &checks,
        Check check,
        bool (*evaluate)(const void *context, std::string &actual)
    ) {
        checks.push_back(std::move(check));
        const Check &stored = checks.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(evaluate, &stored, stored.name_.c_str())
        );
    }

    void addOccupancyExpectation(
        int partIndex, IRMath::ivec3 local, bool expectOccupied, std::string name
    ) {
        m_recipe.checks_.push_back(
            OccupancyCheck{
                local,
                partIndex,
                expectOccupied,
                CheckSource::VOXEL_ALPHA,
                std::nullopt,
                std::move(name)
            }
        );
        const OccupancyCheck &check = m_recipe.checks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateOccupancyCheck, &check, check.name_.c_str())
        );
    }

  public:
    // Assert the POOL's active-mask bit for the cell, rather than the voxel
    // record's alpha. The two are the same fact stored twice — CPU-side and
    // GPU-side — and they can only disagree when something wrote the raw
    // `voxels_` span without resyncing the pool. Pair it with an
    // expectOccupancy on the same cell: alpha alone passes on a set that would
    // render the wrong pose, because the compact shader never reads alpha.
    void expectPoolActive(IRMath::ivec3 local, bool expectActive, std::string name) {
        m_recipe.checks_.push_back(
            OccupancyCheck{
                local,
                -1,
                expectActive,
                CheckSource::POOL_ACTIVE_MASK,
                std::nullopt,
                std::move(name)
            }
        );
        const OccupancyCheck &check = m_recipe.checks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateOccupancyCheck, &check, check.name_.c_str())
        );
    }

    // Assert the cell is occupied AND carries palette colour @p paletteIndex —
    // the positive fire for selectPaletteSwatch. Occupancy alone cannot tell a
    // swatch click that landed from one swallowed by the panel background: the
    // voxel is placed either way, just in the previously-active colour.
    void expectVoxelColor(IRMath::ivec3 local, int paletteIndex, std::string name) {
        if (paletteIndex < 0 || paletteIndex >= kPaletteCount) {
            recordError(
                "palette swatch " + std::to_string(paletteIndex) + " out of range in check " + name
            );
            return;
        }
        m_recipe.checks_.push_back(
            OccupancyCheck{
                local,
                -1,
                true,
                CheckSource::VOXEL_ALPHA,
                kPaletteColors[paletteIndex],
                std::move(name)
            }
        );
        const OccupancyCheck &check = m_recipe.checks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateOccupancyCheck, &check, check.name_.c_str())
        );
    }

    // Assert the ray from the parked cursor lands on `local` — the aim check
    // that pairs with a hover op. Evaluates at end of segment, so the hover it
    // pairs with must be the segment's LAST cursor-moving event; to arm an aim
    // check ahead of a click, put hover+expectPick in their own segment before
    // the click's segment (see segment()).
    void expectPick(IRMath::ivec3 local, std::string name) {
        m_recipe.pickChecks_.push_back(
            PickCheck{IRMath::roundVec3HalfUp(m_model.worldCenter(local)), std::move(name)}
        );
        const PickCheck &check = m_recipe.pickChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluatePickCheck, &check, check.name_.c_str())
        );
    }

    // Assert that every texel showing the editable set picks the face it shows
    // when this segment settles — the check an aim-based assertion cannot make,
    // since the aim and the pick share one screen mapping and would agree with
    // each other even if both were offset from the render.
    void expectPickMatchesRender(std::string name) {
        m_recipe.pickChecks_.push_back(PickCheck{IRMath::ivec3(0), std::move(name)});
        const PickCheck &check = m_recipe.pickChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluatePickMatchesRender, nullptr, check.name_.c_str())
        );
    }

    // Assert the fill preview is drawn on the cells `a`..`b` when this segment
    // settles: the positive fire for holdBox. It reads the pixels the ghost
    // owns, so it cannot pass on a ghost that is hidden behind the voxels it
    // would carve; an erase drag has no place for it.
    void expectFillGhost(IRMath::ivec3 a, IRMath::ivec3 b, std::string name) {
        addPredicateCheck(
            m_recipe.fillGhostChecks_,
            FillGhostCheck{
                true,
                IRMath::roundVec3HalfUp(m_model.worldCenter(IRMath::min(a, b))),
                IRMath::roundVec3HalfUp(m_model.worldCenter(IRMath::max(a, b))),
                std::move(name)
            },
            &evaluateFillGhostCheck
        );
    }

    // Assert no fill preview is drawn when this segment settles.
    void expectNoFillGhost(std::string name) {
        addPredicateCheck(
            m_recipe.fillGhostChecks_,
            FillGhostCheck{false, IRMath::ivec3(0), IRMath::ivec3(0), std::move(name)},
            &evaluateFillGhostCheck
        );
    }

    // Assert a slider's live value when this segment settles — the positive
    // fire for dragGuiSlider: a drag that missed the track and landed on the
    // panel background never presses the widget, so its value stays wherever
    // it started and this fails instead of quietly passing.
    void expectSliderValue(SliderTarget target, float expected, float tolerance, std::string name) {
        addPredicateCheck(
            m_recipe.sliderChecks_,
            SliderCheck{target, expected, tolerance, std::move(name)},
            &evaluateSliderCheck
        );
    }

    // Assert part @p partIndex's LOD-gate verdict and pin when this segment
    // settles. @p pinnedTier -1 asserts the part carries no override.
    void expectPartGated(int partIndex, bool expectGated, int pinnedTier, std::string name) {
        addPredicateCheck(
            m_recipe.partGateChecks_,
            PartGateCheck{partIndex, expectGated, pinnedTier, std::move(name)},
            &evaluatePartGateCheck
        );
    }

    void expectPreviewGated(bool expectGated, int pinnedTier, std::string name) {
        addPredicateCheck(
            m_recipe.partGateChecks_,
            PartGateCheck{-1, expectGated, pinnedTier, std::move(name)},
            &evaluatePartGateCheck
        );
    }

    // Assert part @p partIndex's voxel extent when this segment settles.
    void expectPartExtent(int partIndex, IRMath::ivec3 expected, std::string name) {
        addPredicateCheck(
            m_recipe.partExtentChecks_,
            PartExtentCheck{partIndex, expected, std::move(name)},
            &evaluatePartExtentCheck
        );
    }

    // Assert the editor's mirror state when this segment settles: the axes
    // this builder enabled, each seated at its component of @p offsets (a
    // disabled axis's component is ignored). The model reflects through the
    // same planes, so an expectation the model disagrees with is a recipe error.
    void expectMirrorOffsets(IRMath::vec3 offsets, std::string name) {
        SymmetryState expected = m_symmetry;
        expected.offsetX_ = offsets.x;
        expected.offsetY_ = offsets.y;
        expected.offsetZ_ = offsets.z;
        if (!mirrorPlanesMatch(m_symmetry, expected)) {
            recordError("mirror check " + name + " disagrees with the planes the model seats");
            return;
        }
        addPredicateCheck(
            m_recipe.mirrorChecks_,
            MirrorCheck{expected, std::move(name)},
            &evaluateMirrorCheck
        );
    }

    // Assert whether loft-mask cell @p cell is painted when this segment
    // settles: the positive fire for clickLoftCell.
    void expectLoftMask(LoftMask mask, IRMath::ivec2 cell, bool expectOn, std::string name) {
        addPredicateCheck(
            m_recipe.loftMaskChecks_,
            LoftMaskCheck{mask, cell, expectOn, std::move(name)},
            &evaluateLoftMaskCheck
        );
    }

    void expectManifestContains(std::string text, std::string name) {
        expectManifest(std::move(text), true, std::move(name));
    }

    void expectManifestOmits(std::string text, std::string name) {
        expectManifest(std::move(text), false, std::move(name));
    }

    // Assert the loaded module registered `componentName` with `fieldCount`
    // fields, read through the registry enumeration.
    void expectComponentRegistered(std::string componentName, int fieldCount, std::string name) {
        m_recipe.componentChecks_.push_back(
            ComponentCheck{std::move(componentName), fieldCount, std::move(name)}
        );
        const ComponentCheck &check = m_recipe.componentChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateComponentCheck, &check, check.name_.c_str())
        );
    }

    // Assert the docked module panel `panelName` draws a label
    // reading `label`.
    void expectPanelLabel(std::string panelName, std::string label, std::string name) {
        m_recipe.panelLabelChecks_.push_back(
            PanelLabelCheck{std::move(panelName), std::move(label), std::move(name)}
        );
        const PanelLabelCheck &check = m_recipe.panelLabelChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluatePanelLabelCheck, &check, check.name_.c_str())
        );
    }

    void expectRecipePager(bool present, std::string name) {
        m_recipe.recipePagerChecks_.push_back(RecipePagerCheck{present, std::move(name)});
        const RecipePagerCheck &check = m_recipe.recipePagerChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateRecipePagerCheck, &check, check.name_.c_str())
        );
    }

    // Assert component `component`'s field `field` on entity-scene `target`
    // reads `expected` (or, with `expectEqual` false, does not), or with a
    // nullopt `expected`, that the target lacks the component.
    void expectComponentValue(
        int target,
        std::string component,
        std::string field,
        std::optional<ComponentFieldValue> expected,
        bool expectEqual,
        std::string name
    ) {
        m_recipe.componentValueChecks_.push_back(
            ComponentValueCheck{
                target,
                std::move(component),
                std::move(field),
                std::move(expected),
                expectEqual,
                std::move(name)
            }
        );
        const ComponentValueCheck &check = m_recipe.componentValueChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateComponentValueCheck, &check, check.name_.c_str())
        );
    }

    // Reject the recipe outright. For preconditions the op vocabulary cannot
    // express — a scene too small to hold the entity, say — where the ops
    // would each still "work" and quietly author a clipped shape. Same
    // channel as an unaimable gesture: main() logs it and exits non-zero.
    void recordError(std::string why) {
        m_recipe.errors_.push_back(std::move(why));
    }

    // Closes the last segment and hands back the recipe. The shot table is NOT
    // built here — see resolveShots.
    Recipe finish() {
        if (m_heldBox)
            recordError("the recipe ends with a box held: holdBox needs a releaseBox");
        if (m_heldGizmo)
            recordError(
                "the recipe ends with a gizmo held: holdPartGizmo needs a releasePartGizmo"
            );
        flushSegment();
        return std::move(m_recipe);
    }

  private:
    void expectManifest(std::string text, bool expectContains, std::string name) {
        addPredicateCheck(
            m_recipe.manifestChecks_,
            ManifestCheck{std::move(text), expectContains, std::move(name)},
            &evaluateManifestCheck
        );
    }

    // What the selected part's recipe binding is, as far as the model knows.
    struct PartBinding {
        std::optional<PartRecipeBinding> binding_;
        bool known_ = true;
    };

    struct RemovedPartModel {
        OccupancyModel model_;
        int index_ = 0;
        PartBinding binding_;
    };

    // After an undo, redo, clear or reload the editor holds bindings the
    // model does not re-read.
    void forgetBindings() {
        for (PartBinding &binding : m_partBindings)
            binding.known_ = false;
    }

    // Applies the editor's handle re-apply to @p model: a cell the recipe made
    // only at @p oldValues is cleared, and every cell it makes at @p newValues
    // is set.
    bool replaceRecipeCells(
        OccupancyModel &model,
        std::size_t recipeIndex,
        const std::vector<RecipeClassMaskEntry> &mask,
        const std::vector<float> &oldValues,
        const std::vector<float> &newValues,
        std::string &error
    ) const {
        std::vector<RecipeCell> oldCells;
        std::vector<RecipeCell> newCells;
        if (!m_moduleView->evaluate_(recipeIndex, oldValues, model.size(), mask, oldCells, error) ||
            !m_moduleView->evaluate_(recipeIndex, newValues, model.size(), mask, newCells, error))
            return false;
        for (const RecipeCell &cell : oldCells) {
            const bool kept =
                std::any_of(newCells.begin(), newCells.end(), [&cell](const RecipeCell &next) {
                    return next.local_ == cell.local_;
                });
            if (!kept)
                model.set(cell.local_, false);
        }
        for (const RecipeCell &cell : newCells)
            model.set(cell.local_, true);
        return true;
    }

    void recordSelectedPartRemoval() {
        m_removedPartModel = RemovedPartModel{
            m_model,
            m_activePart,
            m_partBindings[static_cast<std::size_t>(m_activePart)]
        };
        m_partModels.erase(m_partModels.begin() + m_activePart);
        m_partBindings.erase(m_partBindings.begin() + m_activePart);
        if (m_partModels.empty()) {
            m_activePart = -1;
            return;
        }
        m_activePart = IRMath::min(m_activePart, static_cast<int>(m_partModels.size()) - 1);
        m_model = m_partModels[static_cast<std::size_t>(m_activePart)];
    }

    struct TypedKey {
        IRInput::KeyMouseButtons key_;
        bool shift_ = false;
    };

    static std::optional<TypedKey> typedKey(char c) {
        const auto offset = [](IRInput::KeyMouseButtons first, int index) {
            return static_cast<IRInput::KeyMouseButtons>(static_cast<int>(first) + index);
        };
        if (c >= 'a' && c <= 'z')
            return TypedKey{offset(IRInput::kKeyButtonA, c - 'a')};
        if (c >= 'A' && c <= 'Z')
            return TypedKey{offset(IRInput::kKeyButtonA, c - 'A'), true};
        if (c >= '0' && c <= '9')
            return TypedKey{offset(IRInput::kKeyButton0, c - '0')};
        switch (c) {
        case ' ':
            return TypedKey{IRInput::kKeyButtonSpace};
        case '.':
            return TypedKey{IRInput::kKeyButtonPeriod};
        case ',':
            return TypedKey{IRInput::kKeyButtonComma};
        case '-':
            return TypedKey{IRInput::kKeyButtonMinus};
        case '/':
            return TypedKey{IRInput::kKeyButtonSlash};
        case '=':
            return TypedKey{IRInput::kKeyButtonEqual};
        case '[':
            return TypedKey{IRInput::kKeyButtonLeftBracket};
        case ']':
            return TypedKey{IRInput::kKeyButtonRightBracket};
        case '{':
            return TypedKey{IRInput::kKeyButtonLeftBracket, true};
        case '}':
            return TypedKey{IRInput::kKeyButtonRightBracket, true};
        case '\'':
            return TypedKey{IRInput::kKeyButtonApostrophe};
        case '"':
            return TypedKey{IRInput::kKeyButtonApostrophe, true};
        default:
            return std::nullopt;
        }
    }

    std::optional<IRMath::vec3> aimFor(IRMath::ivec3 target) const {
        return m_eraseMode ? m_model.aimAtVoxel(target) : m_model.aimToPlace(target);
    }

    // The active model after a BAKE of the default shape row whose P1 slider
    // holds @p radius.
    OccupancyModel sphereBakedAt(float radius) const {
        const IRMath::SDF::ShapeType type =
            IRVoxelEditor::kBakeShapeTypes[IRVoxelEditor::kBakeDefaultShapeRow];
        OccupancyModel baked = m_model;
        baked.bakeShape(
            type,
            IRVoxelEditor::bakeShapeParams(type, radius, IRVoxelEditor::kBakeParam2Default),
            m_symmetry
        );
        return baked;
    }

    // The editor's press-then-release-without-moving gesture at `worldAim`.
    void emitClick(IRMath::vec3 worldAim) {
        emitMove(worldAim);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
    }

    void emitMove(IRMath::vec3 worldAim) {
        IRVideo::GuiInputEvent event{};
        event.frameOffset_ = m_frame;
        event.type_ = IRVideo::GuiInputEvent::Type::MOVE;
        m_current.aims_.push_back(AimFixup{static_cast<int>(m_current.events_.size()), worldAim});
        m_current.events_.push_back(event);
        m_frame += kFramesPerClickStep;
    }

    // Cursor MOVE onto a point on the GUI canvas. Same deferred-pixel scheme as
    // emitMove, through the GUI mapping instead of the world one.
    void emitGuiMove(IRMath::vec2 guiTrixel) {
        IRVideo::GuiInputEvent event{};
        event.frameOffset_ = m_frame;
        event.type_ = IRVideo::GuiInputEvent::Type::MOVE;
        m_current.guiAims_.push_back(
            GuiAimFixup{static_cast<int>(m_current.events_.size()), guiTrixel}
        );
        m_current.events_.push_back(event);
        m_frame += kFramesPerClickStep;
    }

    // Shared body of prevFrame / nextFrame. The shadow model is swapped exactly
    // as switchToFrame swaps the live voxels, so aiming survives a frame step.
    void stepFrame(int delta) {
        const int target = m_activeFrame + delta;
        if (target < 0 || target >= static_cast<int>(m_frameModels.size()) + 1) {
            recordError(
                "frame step to " + std::to_string(target) + " is out of range [0," +
                std::to_string(m_frameModels.size() + 1) + ") in segment " + m_current.label_ +
                " — switchToFrame clamps, so the live editor would stay put"
            );
            return;
        }
        tapKey(delta < 0 ? IRInput::kKeyButtonLeft : IRInput::kKeyButtonRight);
        // m_frameModels holds every frame BUT the active one, which lives in
        // m_model (the editor's hot slot). Park the departing frame back in the
        // list at its own index and take the arriving one out.
        m_frameModels.insert(m_frameModels.begin() + m_activeFrame, m_model);
        m_model = m_frameModels[static_cast<std::size_t>(target)];
        m_frameModels.erase(m_frameModels.begin() + target);
        m_activeFrame = target;
    }

    // Wheel notches at the cursor's current pixel. GLFW reports a wheel-down
    // notch as a negative y offset, so a positive row count injects negative y.
    void emitWheel(int rows) {
        const float notch = rows > 0 ? -1.0f : 1.0f;
        for (int i = 0; i < IRMath::abs(rows); ++i) {
            IRVideo::GuiInputEvent event{};
            event.frameOffset_ = m_frame;
            event.type_ = IRVideo::GuiInputEvent::Type::SCROLL;
            event.scroll_ = IRMath::vec2(0.0f, notch);
            m_current.events_.push_back(event);
            m_frame += kFramesPerClickStep;
        }
    }

    void emitButton(IRVideo::GuiInputEvent::Type type, IRInput::KeyMouseButtons button) {
        IRVideo::GuiInputEvent event{};
        event.frameOffset_ = m_frame;
        event.type_ = type;
        event.button_ = button;
        // Button events reuse whatever pixel the last MOVE left the cursor at;
        // the harness only applies screenPx_ on MOVE.
        m_current.events_.push_back(event);
        m_frame += kFramesPerClickStep;
    }

    struct HeldBox {
        IRMath::ivec3 a_;
        IRMath::ivec3 b_;
    };

    // Shared body of dragBox and holdBox: press on `a`, move to `b`, and leave
    // the button down for releaseBox. False, with nothing emitted, when either
    // cell cannot be aimed or a box is already held.
    bool pressBox(const char *op, IRMath::ivec3 a, IRMath::ivec3 b) {
        if (m_heldBox) {
            recordError(std::string(op) + " while a box is held in segment " + m_current.label_);
            return false;
        }
        if (m_heldGizmo) {
            recordError(std::string(op) + " while a gizmo is held in segment " + m_current.label_);
            return false;
        }
        // Both cells are aimed against the model as it stands at PRESS time:
        // the fill only commits on RELEASE, so no voxel appears in between.
        const std::optional<IRMath::vec3> aimA = aimFor(a);
        if (!aimA) {
            recordUnreachable((std::string(op) + " start").c_str(), a);
            return false;
        }
        const std::optional<IRMath::vec3> aimB = aimFor(b);
        if (!aimB) {
            recordUnreachable((std::string(op) + " end").c_str(), b);
            return false;
        }
        emitMove(*aimA);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitMove(*aimB);
        // One idle frame so the editor's HELD branch samples the moved cursor
        // before a release commits the fill.
        m_frame += kFramesPerClickStep;
        m_heldBox = HeldBox{a, b};
        return true;
    }

    // Shared body of dragPartGizmo and holdPartGizmo: press on @p from, move to
    // @p to, and leave the button down for releasePartGizmo.
    bool
    pressPartGizmo(const char *op, IRMath::vec3 from, IRMath::vec3 to, IRMath::vec3 partDelta) {
        if (m_heldBox || m_heldGizmo) {
            recordError(std::string(op) + " while a button is held in segment " + m_current.label_);
            return false;
        }
        if (m_partModels.empty()) {
            recordError(std::string(op) + " needs an entity-scene part");
            return false;
        }
        emitMove(from);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitMove(to);
        // One idle frame so GIZMO_DRAG applies the moved cursor before a
        // release ends the drag.
        m_frame += kFramesPerClickStep;
        m_model.setOrigin(m_model.origin() + partDelta);
        m_heldGizmo = true;
        return true;
    }

    // An op the model could not aim: no camera-facing face that would land the
    // edit is visible with margin. The remedy is to re-order the recipe so one
    // is exposed.
    void recordUnreachable(const char *op, IRMath::ivec3 target) {
        m_recipe.errors_.push_back(
            std::string(op) + " at local (" + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z) +
            "): no camera-facing face that would land the edit is visible in segment " +
            m_current.label_
        );
    }

    void flushSegment() {
        if (m_current.events_.empty() && m_current.assertions_.empty())
            return;
        m_recipe.segments_.push_back(std::move(m_current));
        m_current = Segment{};
    }

    // The editor re-seats its mirror planes whenever its editable set changes;
    // the model follows on every change of active model.
    void reseatMirrors() {
        seatEnabledMirrorAxes(m_symmetry, m_model.size());
    }

    OccupancyModel m_model;
    // The single-set scene's extent: the PART SIZE sliders' range and initial
    // value, and the loft grids' cell counts.
    IRMath::ivec3 m_sceneSize;
    // What the PART SIZE sliders read: the extent the next addVoxelPart creates.
    IRMath::ivec3 m_partSize;
    const ModuleSessionView *m_moduleView = nullptr;
    // Scroll shadows of the RECIPES and COMPONENTS lists. The editor builds
    // both once, at offset 0, with the module view's items in its order, and
    // only the wheel moves them.
    IRComponents::C_WidgetList m_recipeList;
    IRComponents::C_WidgetList m_componentList;
    // The RECIPES row the live list has selected: row 0 at build, then the
    // row applyRecipe clicked or a handle drag re-pointed it to.
    int m_recipeListSelected = 0;
    std::vector<OccupancyModel> m_partModels;
    // Parallel to m_partModels.
    std::vector<PartBinding> m_partBindings;
    int m_activePart = -1;
    std::optional<RemovedPartModel> m_removedPartModel;
    // The animation's non-active frames, indexed as the editor indexes them
    // with the active frame removed — m_model IS frame m_activeFrame, mirroring
    // the editor's hot-slot/cold-storage split (animation.hpp). Empty until a
    // recipe duplicates a frame, so single-frame sessions carry no extra state.
    std::vector<OccupancyModel> m_frameModels;
    int m_activeFrame = 0;
    Recipe m_recipe;
    Segment m_current;
    int m_frame = 0;
    int m_segmentIndex = 0;
    IRMath::ivec2 m_lastDraggedSliderPos{-1};
    int m_lastSliderDragSegment = 0;
    bool m_eraseMode = false;
    bool m_classPaintMode = false;
    // G toggles it; the editor starts with it off.
    bool m_transformMode = false;
    // Q or E turned the camera in the current segment; the next segment opens
    // at the baseline yaw.
    bool m_cameraTurned = false;
    SymmetryState m_symmetry;
    // The box a holdBox pressed and no releaseBox has released yet.
    std::optional<HeldBox> m_heldBox;
    // A holdPartGizmo no releasePartGizmo has released yet.
    bool m_heldGizmo = false;
    // The census expectGizmoHandleTexelsUnchanged compares against.
    const GizmoTexelCheck *m_lastGizmoTexels = nullptr;
};

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSION_BUILDER_H */
