#ifndef IR_VOXEL_EDITOR_SESSION_BUILDER_H
#define IR_VOXEL_EDITOR_SESSION_BUILDER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/input/ir_input_types.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/common/array_transforms.hpp>

#include "anim_panel.hpp"
#include "array_panel.hpp"
#include "component_records.hpp"
#include "lod_panel.hpp"
#include "palette.hpp"
#include "symmetry.hpp"

#include <deque>
#include <optional>
#include <string>
#include <string_view>
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

// Camera zoom sessions author at by default: every face is 8 x 8 screen pixels,
// so a capture reads clearly. A face aim resolves down to zoom 1.
inline constexpr float kSessionZoom = 4.0f;

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

    // The editor seeds the editable set with a full ground plane at the far z
    // slice so the first click has something to land on (main.cpp initEntities).
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

struct PartAuthoredCountCheck {
    int partIndex_ = 0;
    int expected_ = 0;
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

// Pick expectation evaluated through the editor's own edit pick at a shot's
// capture frame: the world voxel the parked cursor must land on.
struct PickCheck {
    IRMath::ivec3 worldVoxel_ = IRMath::ivec3(0);
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

// The last-saved entity-scene manifest contains text_ and was written by this
// run, so a manifest left by an earlier run cannot satisfy it.
struct ManifestCheck {
    std::string text_;
    std::string name_;
};

// Module-panel expectation: the docked panel named panelName_ holds exactly
// one label, reading label_.
struct PanelLabelCheck {
    std::string panelName_;
    std::string label_;
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
    std::deque<PartAuthoredCountCheck> partAuthoredCountChecks_;
    std::deque<PartEditableCheck> partEditableChecks_;
    std::deque<CanvasCountCheck> canvasCountChecks_;
    std::deque<RotationModeCheck> rotationModeChecks_;
    // Same stable-storage contract as checks_, for expectSliderValue.
    std::deque<SliderCheck> sliderChecks_;
    // Same stable-storage contract as checks_, for expectPick.
    std::deque<PickCheck> pickChecks_;
    // Same contract, for the module checks.
    std::deque<ComponentCheck> componentChecks_;
    std::deque<PanelLabelCheck> panelLabelChecks_;
    std::deque<ComponentValueCheck> componentValueChecks_;
    // Same contract, for the entity-scene LOD checks.
    std::deque<PartGateCheck> partGateChecks_;
    std::deque<ManifestCheck> manifestChecks_;
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
bool evaluatePartAuthoredCountCheck(const void *context, std::string &actual);
bool evaluatePartEditableCheck(const void *context, std::string &actual);
bool evaluateCanvasCountCheck(const void *context, std::string &actual);
bool evaluateRotationModeCheck(const void *context, std::string &actual);

// Reads one PickCheck through the editor's edit pick. Same PREDICATE channel
// as evaluateOccupancyCheck. Defined in main.cpp, beside the pick itself.
bool evaluatePickCheck(const void *context, std::string &actual);

// Sweeps the rendered frame for agreement with the edit pick; takes no
// context. Defined in main.cpp.
bool evaluatePickMatchesRender(const void *context, std::string &actual);

// Reads one SliderCheck against the live widget its target_ names. Same
// PREDICATE channel as evaluateOccupancyCheck, for the same reason: the
// widget entity ids don't exist at recipe-build time. Defined in main.cpp,
// where the ANIM panel's slider entity handles live.
bool evaluateSliderCheck(const void *context, std::string &actual);

// Reads one ComponentCheck / PanelLabelCheck against the loaded module.
// Defined in main.cpp, where the module host and its docked panels live.
bool evaluateComponentCheck(const void *context, std::string &actual);
bool evaluatePanelLabelCheck(const void *context, std::string &actual);

// Read one PartGateCheck / ManifestCheck against the live entity scene and the
// file its last save wrote. Defined in main.cpp, where the scene lives.
bool evaluatePartGateCheck(const void *context, std::string &actual);
bool evaluateManifestCheck(const void *context, std::string &actual);

// Reads one ComponentValueCheck against the entity scene. Defined in main.cpp,
// where the entity scene and the module's Lua state live.
bool evaluateComponentValueCheck(const void *context, std::string &actual);

// Builds a Recipe from editor gestures. Every op appends to the current
// segment; segment(label) closes the current one and starts the next. Ops that
// cannot be aimed record an error instead of emitting a bogus click.
class Builder {
  public:
    Builder(std::string name, IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin)
        : m_model(sceneSize, sceneOrigin)
        , m_sceneOrigin(sceneOrigin) {
        m_recipe.name_ = std::move(name);
        m_model.seedGroundPlane();
        segment("start");
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
    void segment(const char *label, float zoom = kSessionZoom) {
        flushSegment();
        m_current = Segment{};
        m_current.label_ = m_recipe.name_ + "_" + label;
        m_current.zoom_ = zoom;
        m_frame = 0;
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
        const std::optional<IRMath::vec3> aimA = aimFor(a);
        if (!aimA) {
            recordUnreachable("dragBox start", a);
            return;
        }
        emitMove(*aimA);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        // The drag's end cell is aimed against the model as it stands at PRESS
        // time — the fill only commits on RELEASE, so no voxel has appeared yet.
        const std::optional<IRMath::vec3> aimB = aimFor(b);
        if (!aimB) {
            recordUnreachable("dragBox end", b);
            return;
        }
        emitMove(*aimB);
        // One idle frame so the editor's HELD branch samples the moved cursor
        // before the release commits the fill.
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
        m_model.setBoxMirrored(a, b, !m_eraseMode, m_symmetry);
    }

    // Tap a key with no modifier (V erase-mode toggle, X/Y/Z mirrors, K layer).
    void tapKey(IRInput::KeyMouseButtons key) {
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, key);
    }

    // Tap a key while a modifier is held. The modifier leads the key by two
    // frames so it is already held when the key press drains.
    void chordKey(IRInput::KeyMouseButtons modifier, IRInput::KeyMouseButtons key) {
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, modifier);
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, modifier);
    }

    void toggleEraseMode() {
        tapKey(IRInput::kKeyButtonV);
        m_eraseMode = !m_eraseMode;
    }

    // Enable mirror-symmetry axes for subsequent edits. Taps
    // the editor's X/Y/Z toggles so the live editor mirrors each edit, and turns
    // on the shadow model's mirroring against the same scene-centre planes the
    // editor uses (SymmetryState offsets set to (size-1)/2 in main() — the
    // editor mirrors around that plane, so the model must too). Call once with
    // every axis to enable; assumes symmetry starts off (the editor's default).
    void enableSymmetry(bool x, bool y, bool z) {
        const IRMath::ivec3 size = m_model.size();
        if (x) {
            tapKey(IRInput::kKeyButtonX);
            m_symmetry.enableX_ = true;
            m_symmetry.offsetX_ = mirrorCenterOffset(size.x);
        }
        if (y) {
            tapKey(IRInput::kKeyButtonY);
            m_symmetry.enableY_ = true;
            m_symmetry.offsetY_ = mirrorCenterOffset(size.y);
        }
        if (z) {
            tapKey(IRInput::kKeyButtonZ);
            m_symmetry.enableZ_ = true;
            m_symmetry.offsetZ_ = mirrorCenterOffset(size.z);
        }
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
    }

    void addVoxelPart() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonP);
        if (m_partModels.empty()) {
            OccupancyModel first(m_model.size(), m_sceneOrigin);
            first.seedGroundPlane();
            m_partModels.push_back(first);
            m_activePart = 0;
            m_model = std::move(first);
            return;
        }
        m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        OccupancyModel next(m_model.size(), m_sceneOrigin);
        next.seedGroundPlane();
        m_partModels.push_back(next);
        m_activePart = static_cast<int>(m_partModels.size()) - 1;
        m_model = std::move(next);
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
    }

    void dragWorld(IRMath::vec3 from, IRMath::vec3 to) {
        emitMove(from);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitMove(to);
        m_frame += kFramesPerClickStep;
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
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

    void applyRadialArray(int count) {
        dragGuiSlider(kArrayCountSliderGeometry, 2.0f, 12.0f, static_cast<float>(count));
        clickGui(kArrayApplyCenter);
        if (m_partModels.empty()) {
            recordError("radial array needs an entity-scene part");
            return;
        }
        m_partModels[static_cast<std::size_t>(m_activePart)] = m_model;
        const OccupancyModel source = m_model;
        const auto offsets = IRPrefab::Arrays::radial(count, IRMath::vec3(0.0f, 0.0f, 1.0f), 4.0f);
        for (const auto &offset : offsets) {
            OccupancyModel copy = source;
            copy.setOrigin(source.origin() + offset.translation_);
            m_partModels.push_back(std::move(copy));
        }
        m_activePart = static_cast<int>(m_partModels.size()) - 1;
        m_model = m_partModels.back();
    }

    void applyRadialArrayToLiveScene(int count) {
        dragGuiSlider(kArrayCountSliderGeometry, 2.0f, 12.0f, static_cast<float>(count));
        clickGui(kArrayApplyCenter);
    }

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

  private:
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

    void expectManifestContains(std::string text, std::string name) {
        m_recipe.manifestChecks_.push_back(ManifestCheck{std::move(text), std::move(name)});
        const ManifestCheck &check = m_recipe.manifestChecks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateManifestCheck, &check, check.name_.c_str())
        );
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

    // Assert the docked module panel `panelName` built exactly one label,
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
        flushSegment();
        return std::move(m_recipe);
    }

  private:
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

    OccupancyModel m_model;
    IRMath::vec3 m_sceneOrigin;
    std::vector<OccupancyModel> m_partModels;
    int m_activePart = -1;
    // The animation's non-active frames, indexed as the editor indexes them
    // with the active frame removed — m_model IS frame m_activeFrame, mirroring
    // the editor's hot-slot/cold-storage split (animation.hpp). Empty until a
    // recipe duplicates a frame, so single-frame sessions carry no extra state.
    std::vector<OccupancyModel> m_frameModels;
    int m_activeFrame = 0;
    Recipe m_recipe;
    Segment m_current;
    int m_frame = 0;
    bool m_eraseMode = false;
    SymmetryState m_symmetry;
};

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSION_BUILDER_H */
