#ifndef IR_VOXEL_EDITOR_SESSION_BUILDER_H
#define IR_VOXEL_EDITOR_SESSION_BUILDER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/input/ir_input_types.hpp>
#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/picking.hpp>

#include <deque>
#include <optional>
#include <string>
#include <vector>

// Authoring sessions (#766 Part 2c) — compile a recipe of editor gestures into
// the scripted-input streams the GUI-test harness replays against the live UI.
//
// The point of F-1.6 is that the five entities are authored *by using the
// editor*: every voxel lands because a scripted cursor clicked a face and the
// editor's own place/erase path ran. So a recipe never touches voxel storage.
// It names cells; the builder works out which face of which already-placed
// voxel to click, aims the cursor with IRRender::worldPos3DToMouseScreenPx
// (Phase 0's validated world→screen primitive), and emits MOVE / PRESS /
// RELEASE events.
//
// The shadow occupancy model is what makes that aiming reliable. It mirrors the
// editable set's occupancy as the recipe grows it and replays
// IRPrefab::Picking::castVoxelRay's front-to-back walk over that mirror, so an
// aim that would be occluded is caught while the recipe is being built rather
// than as a mystery FAIL (or, worse, a silent no-op) at run time.
//
// Two properties of the editor's isometric view drive the whole design:
//   - The picking ray marches along +(1,1,1), so exactly three faces of a voxel
//     can ever be clicked: -x, -y, -z. Placing at cell T therefore means
//     clicking face n of anchor cell T - n for one of those three normals.
//   - Aim accuracy is zoom-bound. Phase 0 (P0-2) put the floor for hitting the
//     right iso *column* at zoom >= 2; hitting the right *face* of a voxel
//     needs zoom >= 3 (see kSessionZoom), because a face-centre aim is offset
//     half a column-spacing in screen space and rounds across the boundary
//     below that.
//
// Recipes run at the cardinal baseline yaw (no camera rotation between
// segments), which is what lets the model assume the (1,1,1) march axis.
namespace IRVoxelEditor::Session {

// The three faces whose outward normal points back at the camera — the only
// faces a click can land on. Ordered x, y, z: the aim search prefers the side
// faces, so a column grown upward is still reachable from the side later.
inline constexpr IRMath::ivec3 kCameraFacingNormals[3] = {
    IRMath::ivec3(-1, 0, 0),
    IRMath::ivec3(0, -1, 0),
    IRMath::ivec3(0, 0, -1),
};

// How far inside the anchor voxel a face click aims, measured from the voxel
// centre toward the clicked face. Strictly < 0.5 so the point stays inside the
// cube: the picker derives the hit face from the dominant axis of
// (hit point - voxel centre), and a point exactly on the face plane can round
// to either side of it. 0.4 leaves a 0.1-voxel margin to the clicked face while
// the other two axes stay 0.5 away, so the dominant axis survives the cursor
// pixel being rounded to an integer.
inline constexpr float kFaceAimDepth = 0.4f;

// Ray-march step for the occlusion model, in voxels per axis. The picker walks
// iso depth in kPickingDepthStep increments and one iso-depth unit moves the
// recovered world point by (1/3, 1/3, 1/3), so this matches its sampling
// density — the model can't miss a cell the real ray would have hit.
inline constexpr float kRayStep = IRPrefab::Picking::kPickingDepthStep / 3.0f;

// Frames a session shot leaves between the cursor MOVE and the button PRESS,
// and again before the RELEASE. One frame each is what the existing scripted
// shots use (kPaletteClickEvents); the drag ops add a HELD frame on top so the
// editor's drag state machine sees a PRESSED, a HELD, and a RELEASED sample.
inline constexpr int kFramesPerClickStep = 1;

// Camera zoom sessions author at. Phase 0's zoom >= 2 floor covers picking the
// right iso *column*; picking the right *face within* a voxel needs more. A
// face-centre aim sits half a column-spacing off the voxel centre in screen
// space, so at zoom 2 (iso step (4,2) px) it rounds onto the neighbouring
// column and the click edits the wrong cell — measured by sweeping this
// constant against the drag_probe session: zoom 2 fails the side-face aim
// (11 assertions, 2 FAIL), zoom 3 / 4 / 8 all pass 11/11. 4 is the floor plus
// one step of margin.
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

    void setBox(IRMath::ivec3 a, IRMath::ivec3 b, bool on) {
        const IRMath::ivec3 lo{IRMath::min(a.x, b.x), IRMath::min(a.y, b.y), IRMath::min(a.z, b.z)};
        const IRMath::ivec3 hi{IRMath::max(a.x, b.x), IRMath::max(a.y, b.y), IRMath::max(a.z, b.z)};
        IRMath::iterateAABB(lo, hi, [&](int x, int y, int z) { set(IRMath::ivec3(x, y, z), on); });
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

    // The cell IRPrefab::Picking::castVoxelRay would report for a cursor aimed
    // at `worldAim` — the front-most occupied cell along the +(1,1,1) march.
    // Replays the picker's walk over the mirror rather than approximating it,
    // so "is this aim occluded" is answered the same way the editor will answer
    // it at run time.
    std::optional<IRMath::ivec3> pick(IRMath::vec3 worldAim) const {
        const float span = static_cast<float>(m_size.x + m_size.y + m_size.z);
        for (float t = -span; t <= span; t += kRayStep) {
            const IRMath::vec3 point = worldAim + IRMath::vec3(t);
            const IRMath::ivec3 local = IRMath::roundVec3HalfUp(point - m_origin);
            if (occupied(local))
                return local;
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
            const IRMath::vec3 aim = worldCenter(target) + IRMath::vec3(normal) * kFaceAimDepth;
            const std::optional<IRMath::ivec3> hit = pick(aim);
            if (hit && *hit == target)
                return aim;
        }
        return std::nullopt;
    }

    // Where to aim so a place-mode click lands a voxel at `target`. The editor
    // places at (hit voxel + hit face normal), so the anchor is target - normal
    // for one of the camera-facing normals; the anchor must be occupied, the
    // target empty, and the anchor's face unoccluded.
    std::optional<IRMath::vec3> aimToPlace(IRMath::ivec3 target) const {
        if (!inBounds(target) || occupied(target))
            return std::nullopt;
        for (const IRMath::ivec3 &normal : kCameraFacingNormals) {
            const IRMath::ivec3 anchor = target - normal;
            if (!occupied(anchor))
                continue;
            const IRMath::vec3 aim = worldCenter(anchor) + IRMath::vec3(normal) * kFaceAimDepth;
            const std::optional<IRMath::ivec3> hit = pick(aim);
            if (hit && *hit == anchor)
                return aim;
        }
        return std::nullopt;
    }

  private:
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
// per-frame assert callback, exactly as the Phase 0 probe-map shots do.
struct AimFixup {
    int eventIndex_ = 0;
    IRMath::vec3 worldPoint_ = IRMath::vec3(0.0f);
};

// Occupancy expectation evaluated against the real editable set at a shot's
// capture frame. This is what makes a session positive-fire: a recipe that
// silently no-ops (click swallowed by a widget, aim occluded by scene furniture,
// gesture never reaching the place path) fails here instead of quietly saving
// an empty asset.
struct OccupancyCheck {
    IRMath::ivec3 localCell_ = IRMath::ivec3(0);
    bool expectOccupied_ = false;
    std::string name_;
};

// One shot's worth of session: a camera framing, the events that fire under it,
// their aim fixups, and the assertions evaluated once it settles.
struct Segment {
    std::string label_;
    float zoom_ = kSessionZoom;
    std::vector<IRVideo::GuiInputEvent> events_;
    std::vector<AimFixup> aims_;
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

// Builds a Recipe from editor gestures. Every op appends to the current
// segment; segment(label) closes the current one and starts the next. Ops that
// cannot be aimed record an error instead of emitting a bogus click.
class Builder {
  public:
    Builder(std::string name, IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin)
        : m_model(sceneSize, sceneOrigin) {
        m_recipe.name_ = std::move(name);
        m_model.seedGroundPlane();
        segment("start");
    }

    // Close the current segment and open a new one. The camera framing is
    // re-applied per shot, which is also how a session recovers from the A/D
    // frame keys nudging the camera (Phase 0 probe P0-4).
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
        emitMove(*aim);
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, IRInput::kMouseButtonLeft);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, IRInput::kMouseButtonLeft);
        m_model.set(target, !m_eraseMode);
    }

    // Park the cursor on `target`'s clickable face without pressing. Splits
    // "the aim is right" from "the gesture worked" — a hover segment's
    // PICKS_VOXEL says where the ray actually lands, so a failed edit doesn't
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
        m_model.setBox(a, b, !m_eraseMode);
    }

    // Tap a key with no modifier (V erase-mode toggle, X/Y/Z mirrors, K layer).
    void tapKey(IRInput::KeyMouseButtons key) {
        emitButton(IRVideo::GuiInputEvent::Type::PRESS, key);
        emitButton(IRVideo::GuiInputEvent::Type::RELEASE, key);
    }

    // Tap a key while a modifier is held. The modifier leads the key by two
    // frames so it is already held when the key press drains (Phase 0 P0-1).
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

    void save() {
        chordKey(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonS);
    }

    // Assert the live editable set's occupancy at `local` when this segment
    // settles.
    void expectOccupancy(IRMath::ivec3 local, bool expectOccupied, std::string name) {
        m_recipe.checks_.push_back(OccupancyCheck{local, expectOccupied, std::move(name)});
        const OccupancyCheck &check = m_recipe.checks_.back();
        m_current.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateOccupancyCheck, &check, check.name_.c_str())
        );
    }

    // Assert the ray from the parked cursor lands on `local` — the aim check
    // that pairs with a hover op.
    void expectPick(IRMath::ivec3 local, const char *name) {
        const IRMath::ivec3 worldVoxel = IRMath::roundVec3HalfUp(m_model.worldCenter(local));
        m_current.assertions_.push_back(IRPrefab::GuiTest::picksVoxel(worldVoxel, name));
    }

    // Closes the last segment and hands back the recipe. The shot table is NOT
    // built here — see resolveShots.
    Recipe finish() {
        flushSegment();
        return std::move(m_recipe);
    }

  private:
    std::optional<IRMath::vec3> aimFor(IRMath::ivec3 target) const {
        return m_eraseMode ? m_model.aimAtVoxel(target) : m_model.aimToPlace(target);
    }

    void emitMove(IRMath::vec3 worldAim) {
        IRVideo::GuiInputEvent event{};
        event.frameOffset_ = m_frame;
        event.type_ = IRVideo::GuiInputEvent::Type::MOVE;
        m_current.aims_.push_back(AimFixup{static_cast<int>(m_current.events_.size()), worldAim});
        m_current.events_.push_back(event);
        m_frame += kFramesPerClickStep;
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

    void recordUnreachable(const char *op, IRMath::ivec3 target) {
        m_recipe.errors_.push_back(
            std::string(op) + " at local (" + std::to_string(target.x) + "," +
            std::to_string(target.y) + "," + std::to_string(target.z) +
            "): no unoccluded camera-facing anchor in segment " + m_current.label_
        );
    }

    void flushSegment() {
        if (m_current.events_.empty() && m_current.assertions_.empty())
            return;
        m_recipe.segments_.push_back(std::move(m_current));
        m_current = Segment{};
    }

    OccupancyModel m_model;
    Recipe m_recipe;
    Segment m_current;
    int m_frame = 0;
    bool m_eraseMode = false;
};

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_SESSION_BUILDER_H */
