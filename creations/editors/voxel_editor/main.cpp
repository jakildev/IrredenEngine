#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_video.hpp>

// Components
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_joint_name.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/rig_bridge.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera.hpp>

// Gizmo primitives (T-152, F-0.5 Phase 1)
#include <irreden/render/gizmo.hpp>

// Picking + ray-hit struct (T-219)
#include <irreden/render/picking.hpp>

// Widget framework (T-145 / T-177)
#include <irreden/render/widgets.hpp>

// GUI-test assertions (P3, #1796)
#include <irreden/render/gui_test_assertions.hpp>

// Systems
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/render/systems/system_gizmo_screen_space_size.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_camera_scroll_zoom.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_gizmo_hover.hpp>
#include <irreden/render/systems/system_gizmo_drag.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_color_swatch.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>

// Camera prefab namespace (Z-yaw API)
#include <irreden/render/camera.hpp>

// Frame-based animation state (T-214, F-1.4)
#include "animation.hpp"

#include "editor_layer_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

// Symmetry modes (T-212)
#include "symmetry.hpp"

// Scene save/load
#include "scene_io.hpp"
// Rig save/load (joint entities ↔ .rig asset)
#include "rig_scene_io.hpp"

// Loft-tool mask rendering (trixel_rect fillRect, trixel_text renderText,
// mask_grid_painter drawMaskGridOntoCanvas + hitTestGridCell,
// layout mouse-in-GUI-trixels helper).
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/mask_grid_painter.hpp>
#include <irreden/render/layout.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRVoxelEditor {

// Scene + palette config (kept as named constants so the editor never
// inlines a hardcoded dimension — D1 in the T-211 architect direction
// calls out that the size be configurable per scene).
//
// The editable grid dimensions are runtime-configurable via --scene-size W H D
// (#766 Part 2 — the ant needs 20³, the tree ~26 tall). g_editableSceneSize /
// g_editableSceneOrigin default to the historical 16³ scene and are overwritten
// in main() after arg parse. deriveSceneOrigin keeps the scene centred in X/Y
// and pins the seed ground plane (local z == size.z-1) at world z == 3 for any
// height, so authoring recipes and probe cells stay height-agnostic.
constexpr ivec3 kDefaultEditableSceneSize{16, 16, 16};
// The seed ground plane lives at local z == size.z-1; anchoring it to a fixed
// world z keeps the camera framing and authoring recipes stable as the scene
// grows taller (origin.z shifts down by exactly the height increase).
constexpr int kSeedGroundPlaneWorldZ = 3;
inline vec3 deriveSceneOrigin(ivec3 size) {
    return vec3(
        -static_cast<float>(size.x) / 2.0f,
        -static_cast<float>(size.y) / 2.0f,
        static_cast<float>(kSeedGroundPlaneWorldZ - (size.z - 1))
    );
}
ivec3 g_editableSceneSize = kDefaultEditableSceneSize;
vec3 g_editableSceneOrigin = deriveSceneOrigin(kDefaultEditableSceneSize);
constexpr int kPaletteCount = 16;

// 16 distinct palette colors. Indexed in row-major order across the
// 4×4 panel grid. Colors are picked to span hue and value so the
// active-swatch indicator (theme.borderFocused_ outline) reads against
// every cell.
constexpr Color kPaletteColors[kPaletteCount] = {
    Color{220, 80, 80, 255},
    Color{220, 140, 60, 255},
    Color{220, 200, 60, 255},
    Color{120, 200, 60, 255},
    Color{60, 200, 120, 255},
    Color{60, 200, 200, 255},
    Color{60, 140, 220, 255},
    Color{80, 80, 220, 255},
    Color{140, 60, 220, 255},
    Color{220, 60, 200, 255},
    Color{200, 200, 200, 255},
    Color{140, 140, 140, 255},
    Color{60, 60, 60, 255},
    Color{220, 180, 140, 255},
    Color{120, 80, 60, 255},
    Color{240, 240, 240, 255},
};

// Per-bone display colors for the bone selector panel (F-2.7 / #1608).
// Index 0 = identity / unrigged (neutral gray). Indices 1..7 cycle through
// distinct hues so painted bone assignments read clearly against each other.
constexpr int kBoneSwatchCount = 8;
constexpr Color kBoneColors[kBoneSwatchCount] = {
    Color{180, 180, 180, 255}, // identity/unrigged
    Color{220, 80, 80, 255},
    Color{80, 200, 80, 255},
    Color{80, 120, 220, 255},
    Color{220, 200, 60, 255},
    Color{220, 120, 60, 255},
    Color{200, 80, 220, 255},
    Color{60, 200, 220, 255},
};

// Per-stroke undo record. One record per click; per-voxel drag-paint
// is a follow-up — v1 commits one record per single-voxel place/erase
// event. The "stroke" abstraction is still per-architect (D3) so a
// follow-up that adds drag-paint can fold many edits into one record
// without rewiring undo replay.
struct UndoEdit {
    IREntity::EntityId voxelSet_;
    ivec3 localIdx_;
    C_Voxel prev_;
};

struct UndoRecord {
    std::vector<UndoEdit> edits_;

    std::size_t byteSize() const {
        return sizeof(UndoEdit) * edits_.size();
    }
};

// Per-stroke byte budget with whole-stroke eviction (D3b). One mebibyte
// is enough headroom for a 1024-edit stroke (24 KiB) to live ~40 strokes
// deep before the oldest evicts. Applied per frame: each animation frame
// has its own independent undo stack capped at this limit; total in-memory
// undo cost is at most kUndoByteBudget × frameCount.
constexpr std::size_t kUndoByteBudget = 1u << 20;

// Reserve capacity for the per-stroke edits vector at stroke-begin so
// the per-voxel write hot path doesn't allocate (D3-aligned: the
// architect's reference shape pre-sizes the vector at mouse-down with
// the brush AABB). The brush is single-voxel today; the reserve is a
// high-water-mark for the future drag-paint extension.
constexpr std::size_t kUndoStrokeReserve = 1024;

// Module-level state captured into systems via SystemParams. Holds the
// palette swatch entity ids (built at init), the active swatch index
// (driven by swatch clicks), the editable voxel set's entity id, the
// undo stack, and the in-flight stroke buffer.
struct EditorState {
    std::vector<IREntity::EntityId> paletteSwatches_;
    IREntity::EntityId palettePanel_ = IREntity::kNullEntity;
    int activeSwatchIdx_ = 0;
    // Saved at scene creation for the future save-load / serialization
    // pass; runtime place/erase resolves the target set via the picker
    // hit rather than reading this field, so it has no per-frame use.
    IREntity::EntityId editableVoxelSet_ = IREntity::kNullEntity;
    std::deque<UndoRecord> undoRecords_;
    std::size_t undoTotalBytes_ = 0;
    UndoRecord pendingStroke_;

    // Per-frame undo stacks. Index i stores the saved undo state for
    // g_anim.frames_[i] when that frame is not active. The active frame's
    // live undo state always lives in undoRecords_ / undoTotalBytes_
    // (the "hot" slot). switchToFrame swaps the hot slot in and out.
    // Size must equal g_anim.frameCount(); frame-add and frame-delete
    // handlers insert/erase entries in parallel with g_anim.frames_.
    std::vector<std::deque<UndoRecord>> perFrameUndoStacks_;
    std::vector<std::size_t> perFrameUndoBytes_;
};

EditorState g_editor;

// Bone-paint mode state (F-2.7 / #1608). N toggles the mode; while active,
// left-click writes activeBoneIdx_ to C_Voxel.bone_id_ and tints the voxel
// with kBoneColors[activeBoneIdx_] so the assignment is immediately visible.
// boneSwatches_ / bonePanel_ are created in initEntities once per session.
struct BonePaintState {
    bool active_ = false;
    int activeBoneIdx_ = 0;
    std::vector<IREntity::EntityId> boneSwatches_;
    IREntity::EntityId bonePanel_ = IREntity::kNullEntity;
};
BonePaintState g_bonePaint;

// Frame-based animation state (T-214, F-1.4). Each VoxelFrame snapshots
// the editable target's voxel pool span; switchToFrame swaps the live
// voxels in and out. Lives at module scope (not in EditorState) because
// the playback system reads it from a stateless lambda — keeping it as
// a free global avoids threading a pointer through SystemParams just to
// reach the same address every frame.
AnimationState g_anim;

namespace {

constexpr float kRotationSensitivity = 0.004f;

SymmetryState g_symmetry;

// Erase-fill mode (#766 Part 2b). When ON, the left-click place / box / line /
// face-fill gestures ERASE instead of place — each fill path passes
// `place = false` and aims at the hit voxel itself (not the empty cell adjacent
// to the hit face). Toggled with V; reported in the fill-mode status label.
// Fills the Phase-1 gap where a single-voxel right-click was the only erase
// (clearing the seeded ground slab by hand is ~one right-click per cell) and is
// the carve primitive the session-authoring recipes (Part 2c) need. Right-click
// single-voxel erase is unchanged (always erases regardless of this mode).
bool g_eraseMode = false;

struct RotateParams {
    bool firstRotFrame_ = true;
    float prevMouseX_ = 0.0f;
};

// Drag-fill state machine: tracks a left-button drag for box/line fill.
// Ghost entity is created in initEntities and referenced here so the
// endTick lambda can update its position and visibility each frame.
struct FillToolState {
    bool dragging_ = false;
    ivec3 dragStartWorld_ = {};
    IREntity::EntityId dragStartEntity_ = IREntity::kNullEntity;
    IREntity::EntityId ghostEntity_ = IREntity::kNullEntity;
    ivec3 lastEndWorld_ = {};
};
FillToolState g_fillTool;

// Loft-from-profiles tool (A2). Two 2D boolean masks — XZ (front) and YZ
// (side) — rendered as pixel grids on the GUI canvas. Voxels land only
// where both mask projections overlap (CSG of two extrusions).
constexpr ivec2 kLoftGridXZPos{4, 30};  // top-left of XZ cell grid
constexpr ivec2 kLoftGridYZPos{76, 30}; // top-left of YZ cell grid (8 px gap)
constexpr int kLoftCellPx = 4;          // trixel pixels per mask cell

struct LoftToolState {
    bool active_ = false;
    std::vector<bool> maskXZ_; // [x + z * sizeX] — front (XZ) projection
    std::vector<bool> maskYZ_; // [y + z * sizeY] — side (YZ) projection
};
LoftToolState g_loftTool;

// Scripted palette-click: move cursor to a palette swatch, press, release.
// frameOffset_ = 0: move; 1: press; 2: release — settle then captures.
constexpr IRVideo::GuiInputEvent kPaletteClickEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(200, 300)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(200, 300),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(200, 300),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

// Scripted GUI-assert click (P3, #1796): park the cursor over the LAYERS-panel
// list, press, release. The cursor stays put through capture so the hover
// assertion still reads it; the latch catches the one-frame click-fire. The
// list is a large, child-free hover target so the small screen→GUI-trixel
// offset can't push the cursor off it.
constexpr IRVideo::GuiInputEvent kGuiAssertEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(190, 284)},
    {1,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(190, 284),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
    {2,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(190, 284),
     IRMath::vec2(0.0f),
     IRInput::kMouseButtonLeft},
};

// Scripted scene-pick (P3, #1796): move the cursor over the 3D scene (right of
// the left-column GUI panels), so PICKS_VOXEL casts a ray onto a scene voxel —
// the regression net for the screen→world picking alignment.
constexpr IRVideo::GuiInputEvent kPickVoxelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(800, 450)},
};

// --- Phase 0 mechanism probes (#766) --------------------------------------
// De-risk the auto-authoring premise before building session infrastructure:
// prove keyboard→command dispatch, world→screen click mapping, and measure the
// A/D binding overload — all through the live GUI harness on the seed scene.
// These shots append after the stable framings so existing labels and the
// screen→world regression baseline (kPickVoxelShotIndex) stay untouched.

// Probe 1 — keyboard→command dispatch: hold Ctrl, tap S, release. The editor's
// Ctrl+S handler writes data/editor_scene/scene_frame_0.vxs; the runner checks
// the file exists post-run (no screen mapping needed — the lowest-risk probe).
// Ctrl leads S by two frames so the modifier is held when the S press drains.
constexpr IRVideo::GuiInputEvent kProbeSaveEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
    {3,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
};

// Probe 4 — A/D binding overload: tap A. The plain-PRESSED A binding both starts
// a camera-left move AND adds an animation frame (no modifier guard, main.cpp
// ~2130 vs ~2290) — one press fires both. The runner greps the "Added blank
// frame" log to confirm the overload so later sessions re-establish the camera
// after any frame op.
constexpr IRVideo::GuiInputEvent kProbeADEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonA},
    {1,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonA},
};

// Probe 2 (the gate) — world→screen mapping accuracy. Eight central seed
// ground-plane cells (local z == size-1); each probe shot moves the cursor to
// the pixel IRRender::worldPos3DToMouseScreenPx computes for the cell centre,
// then a PICKS_VOXEL assertion checks the ray hit that world voxel. Cells stay
// near the plane centre so all project on-screen at zoom 1.0. The move pixel is
// filled at shot-run time in onGuiAssertFrame (needs the shot's live camera
// state), so g_probeMapMoves is mutable, not constexpr.
constexpr int kProbeMapCount = 8;
constexpr IRMath::ivec3 kProbeMapLocalCells[kProbeMapCount] = {
    {4, 4, 15},
    {11, 11, 15},
    {4, 11, 15},
    {11, 4, 15},
    {7, 7, 15},
    {8, 8, 15},
    {5, 9, 15},
    {9, 5, 15},
};
IRVideo::GuiInputEvent g_probeMapMoves[kProbeMapCount] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(0)},
};

// The i-th probe-map cell snapped into the live scene bounds so the probe
// stays valid under --scene-size: z rides the ground plane (local z ==
// size.z-1), and x/y clamp into the live footprint. The stored cells target a
// 16³ grid (x/y up to 11), so a --scene-size narrower than 12 in x or y would
// otherwise push them off-grid; clamping collapses those onto the nearest edge
// cell (a valid ground cell that still projects on-screen), trading probe
// coverage for a probe that asserts at any footprint. At the default 16³ every
// stored x/y is < 16 and passes through unchanged. Both the assertion target
// (initEntities) and the cursor-move pixel (onGuiAssertFrame) go through this
// helper, so they stay consistent for whatever cell it returns.
inline IRMath::ivec3 probeGroundCell(int i) {
    return IRMath::ivec3(
        IRMath::min(kProbeMapLocalCells[i].x, g_editableSceneSize.x - 1),
        IRMath::min(kProbeMapLocalCells[i].y, g_editableSceneSize.y - 1),
        g_editableSceneSize.z - 1
    );
}

// --- Part 2b (#766) erase-fill mode probe ---------------------------------
// Verifies the erase-fill toggle through the live UI, occlusion-free: synthetic
// V flips g_eraseMode ON, and the capture-frame assertion (emitted in
// onGuiAssertFrame) checks the fill-mode status label the place/erase system
// repaints each frame now reads "ERASE BOX". This exercises the whole control
// path — synthetic key → command dispatch → g_eraseMode → status label —
// without a scene click, so it is immune to the seed scene's occlusion.
//
// Why not a scripted-erase-removes-a-voxel check: the only clean, static,
// click-erasable surface would be the editable ground plane, but the posed
// starter rig (a skinned 31×3×3 bar) blankets the central ground in iso, and
// skinned voxels are not click-erasable (the world→local mapping fails on them).
// Reliable scripted authoring needs the SessionBuilder's occupancy-model aiming
// (Part 2c); that slice adds the functional carve/erase asset checks. The two V
// key events carry no pixel; the shot leaves erase mode ON (harmless — the
// trailing save / A-D probes don't read it).
constexpr IRVideo::GuiInputEvent kProbeEraseEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonV},
    {1,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonV},
};
constexpr int kProbeEraseNumEvents =
    static_cast<int>(sizeof(kProbeEraseEvents) / sizeof(kProbeEraseEvents[0]));

// The fill-mode status label's current text, read by the Part 2b (#766) erase
// probe. Returns "" when the label isn't built yet.
std::string fillModeLabelText();

// GUI-test shot table covering stable render framings plus the scripted-click
// shots. Superset of the previous kShots[] — render-verify labels still match.
// kGuiAssertShotIndex / kPickVoxelShotIndex select the assertion-bearing shots.
constexpr IRVideo::GuiTestShot kGuiTestShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_idle"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_palette_click"}, kPaletteClickEvents, 3},
    {{0.75f, IRMath::vec2(0.0f), 0.0f, "editor_zoom_out"}, nullptr, 0},
    {{1.5f, IRMath::vec2(0.0f), 0.0f, "editor_zoom_in"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_gui_assert"}, kGuiAssertEvents, 3},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_pick_voxel"}, kPickVoxelEvents, 1},
    // Phase 0 probes (#766), appended after the stable shots so their indices
    // stay fixed. The eight mapping-accuracy shots come first (clean read-only
    // picks), then the Ctrl+S dispatch and A/D-overload shots (both mutate state).
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_0"}, &g_probeMapMoves[0], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_1"}, &g_probeMapMoves[1], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_2"}, &g_probeMapMoves[2], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_3"}, &g_probeMapMoves[3], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_4"}, &g_probeMapMoves[4], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_5"}, &g_probeMapMoves[5], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_6"}, &g_probeMapMoves[6], 1},
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_map_7"}, &g_probeMapMoves[7], 1},
    // Part 2b (#766) erase-fill probe — placed after the read-only map shots
    // (which leave the seed scene intact) and before the state-mutating save /
    // A-D probes, so it erases from the still-seeded editable set.
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_erase"},
     kProbeEraseEvents,
     kProbeEraseNumEvents},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_save"}, kProbeSaveEvents, 4},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_ad"}, kProbeADEvents, 2},
};
constexpr int kNumGuiTestShots = static_cast<int>(sizeof(kGuiTestShots) / sizeof(kGuiTestShots[0]));
constexpr int kGuiAssertShotIndex = 4;
constexpr int kPickVoxelShotIndex = 5;
// Phase 0 / Part 2b probe shot indices (#766). Map shots occupy
// [start, start+count); the erase probe, then the dispatch and overload shots
// follow. Derived from kPickVoxelShotIndex so they track any reordering of the
// stable shots.
constexpr int kProbeMapShotStart = kPickVoxelShotIndex + 1;
constexpr int kProbeEraseShotIndex = kProbeMapShotStart + kProbeMapCount;
constexpr int kProbeSaveShotIndex = kProbeEraseShotIndex + 1;
constexpr int kProbeADShotIndex = kProbeSaveShotIndex + 1;
static_assert(
    kProbeADShotIndex + 1 == kNumGuiTestShots,
    "Phase 0 / Part 2b probe shots (#766) must be the final kGuiTestShots entries"
);

// GUI-test assertion tables (P3, #1796). Filled in initEntities once the widget
// entities exist — assertions reference runtime EntityIds, so unlike the shot
// table they can't be constexpr. Index-aligned with kGuiTestShots.
// g_guiAssertLatch is the caller-owned latch the harness's onAssertFrame_
// callback threads through onFrame (CLICK_FIRES latches the one-frame pulse).
IRPrefab::GuiTest::LatchState g_guiAssertLatch;
std::vector<IRPrefab::GuiTest::Assertion> g_shotAssertions[kNumGuiTestShots];

// Forwarder wired to IRVideo::GuiTestConfig::onAssertFrame_. The harness owns
// input + capture timing in engine/video; this hands each frame to the prefab
// evaluator with the shot's assertion table (engine/video can't see widgets).
void onGuiAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (shotIndex < 0 || shotIndex >= kNumGuiTestShots)
        return;
    // Phase 0 probe 2 (#766): fill this probe-map shot's cursor move with the
    // pixel worldPos3DToMouseScreenPx computes for the target cell at the shot's
    // live camera state. This runs before the harness's event phase on the same
    // tick, so the frame-0 MOVE injects the freshly-computed pixel; the write is
    // idempotent across the shot's frames.
    if (shotIndex >= kProbeMapShotStart && shotIndex < kProbeMapShotStart + kProbeMapCount) {
        const int cellIndex = shotIndex - kProbeMapShotStart;
        // Derive the ground-plane z from the live scene size so the probe stays
        // valid under --scene-size (the seed plane is always local z==size.z-1);
        // at the default 16³ this is the cell's stored z==15.
        const IRMath::ivec3 cell = probeGroundCell(cellIndex);
        const IRMath::vec3 worldCenter = g_editableSceneOrigin + IRMath::vec3(cell);
        g_probeMapMoves[cellIndex].screenPx_ = IRRender::worldPos3DToMouseScreenPx(worldCenter);
    }
    // Part 2b (#766) erase probe: after synthetic V toggled erase mode ON, assert
    // (on the capture frame) that g_eraseMode is set and the fill-mode status
    // label the place/erase system repaints each frame now reads "ERASE BOX".
    // Own GUI-ASSERT line in the gui-verify format the prefab evaluator emits —
    // the mode flag + label are editor state engine/video can't see. Occlusion-
    // free (no scene click); the functional scripted-erase check is Part 2c.
    if (shotIndex == kProbeEraseShotIndex) {
        if (isCaptureFrame) {
            const std::string labelText = fillModeLabelText();
            const bool pass = g_eraseMode && labelText == "ERASE BOX";
            IR_LOG_INFO(
                "GUI-ASSERT shot={} label={} kind={} target={} name={} result={} actual={}",
                shotIndex,
                "editor_probe_erase",
                "ERASE_MODE_LABEL",
                0,
                "v_toggles_erase_mode",
                pass ? "PASS" : "FAIL",
                "eraseMode=" + std::string(g_eraseMode ? "ON" : "OFF") + " label=\"" + labelText +
                    "\""
            );
        }
        return; // erase shot has no g_shotAssertions entry
    }
    const auto &assertions = g_shotAssertions[shotIndex];
    if (assertions.empty())
        return; // shots without assertions (idle / zoom framings) skip latch+eval
    IRPrefab::GuiTest::onFrame(
        g_guiAssertLatch,
        shotIndex,
        isCaptureFrame,
        kGuiTestShots[shotIndex].render_.label_,
        assertions.data(),
        static_cast<int>(assertions.size())
    );
}

// Named-layer state for new placements. Commands below let the user create,
// select, rename, and hide layers. Set g_sceneVoxelSetEntity after
// allocating the scene's C_VoxelSetNew so visibility toggles can iterate
// voxels and update their alpha accordingly.
EditorLayerManager g_layerManager;
IREntity::EntityId g_sceneVoxelSetEntity = IREntity::kNullEntity;

// Animation scrubber and FPS slider entity IDs (T-214). Initialized in
// initEntities; accessed by initSystems lambdas and switchToFrame.
IREntity::EntityId g_scrubberSlider = IREntity::kNullEntity;
IREntity::EntityId g_fpsSlider = IREntity::kNullEntity;

// Layer panel widget entity IDs (T-213). Initialized in initEntities.
IREntity::EntityId g_layerPanel = IREntity::kNullEntity;
IREntity::EntityId g_layerList = IREntity::kNullEntity;
IREntity::EntityId g_layerVisCheckbox = IREntity::kNullEntity;
IREntity::EntityId g_layerAddBtn = IREntity::kNullEntity;
IREntity::EntityId g_layerDelBtn = IREntity::kNullEntity;

// Fill-mode status label — top-left status bar updated each frame with the
// active fill mode (BOX / LINE / FACE), the erase-mode prefix, and active
// symmetry axes.
IREntity::EntityId g_fillModeLabel = IREntity::kNullEntity;

// Current text of the fill-mode status label (Part 2b #766 erase probe reads it
// to verify the erase-mode toggle). Empty when the label isn't built yet.
std::string fillModeLabelText() {
    if (g_fillModeLabel == IREntity::kNullEntity)
        return {};
    return IREntity::getComponent<C_WidgetLabel>(g_fillModeLabel).text_;
}

// Parametric shape bake panel widget entity IDs (T-286).
IREntity::EntityId g_bakePanel = IREntity::kNullEntity;
IREntity::EntityId g_bakeShapeList = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam1Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam2Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeButton = IREntity::kNullEntity;

// Skeleton tree panel widget entity IDs (#1607).
IREntity::EntityId g_skeletonPanel = IREntity::kNullEntity;
IREntity::EntityId g_skeletonList = IREntity::kNullEntity;
IREntity::EntityId g_jointRenameInput = IREntity::kNullEntity;
IREntity::EntityId g_jointRenameBtn = IREntity::kNullEntity;
IREntity::EntityId g_jointReparentInput = IREntity::kNullEntity;
IREntity::EntityId g_jointReparentBtn = IREntity::kNullEntity;

// Hover help (#: editor F-series). The EditorHelpRender system draws the help
// text of whatever panel/control the cursor is over into the HELP panel docked
// below the panel stack. g_helpEntries maps a widget to its one-line help and
// is populated in initEntities once the widgets exist; the smallest hovered
// hitbox wins so a control reads more specifically than the panel under it.
struct HelpEntry {
    IREntity::EntityId widget_ = IREntity::kNullEntity;
    const char *text_ = nullptr;
};
std::vector<HelpEntry> g_helpEntries;
constexpr ivec2 kHelpPanelPos{4, 500};
constexpr ivec2 kHelpPanelSize{372, 92};

void logLayerState() {
    for (const auto &r : g_layerManager.layers()) {
        IR_LOG_INFO(
            "  layer {} '{}' visible={} {}",
            r.id_,
            r.name_,
            r.visible_,
            r.id_ == g_layerManager.activeLayerId() ? "<active>" : ""
        );
    }
}

// Iterate the scene voxel set and activate or deactivate every voxel
// whose layer_id_ matches `layerId`. Called when a layer's visibility
// is toggled so the GPU sees the correct alpha immediately.
void applyLayerVisibility(std::uint8_t layerId, bool visible) {
    if (g_sceneVoxelSetEntity == IREntity::kNullEntity)
        return;
    auto &set = IREntity::getComponent<C_VoxelSetNew>(g_sceneVoxelSetEntity);
    set.editVoxels([&](int, C_Voxel &v, vec3) {
        if (v.layer_id_ != layerId)
            return;
        if (visible)
            v.activate();
        else
            v.deactivate();
    });
}

// Apply a single placement / erasure edit to the voxel set, appending
// the prior state to the in-flight stroke buffer. Skips the edit when
// the target index is out of bounds for the set. `flat` is the linear
// pool index so the per-voxel mutation reuses the precomputed offset.
// `boneId` defaults to 0 (identity) for normal color-paint; pass the
// active bone index when in bone-paint mode (#1608). Writes the raw
// `voxels_` span directly — callers always finish a batch of these
// (single edit, line, AABB, face-fill, or SDF bake) with one
// `commitStroke()`, which resyncs derived state once for the whole batch.
void applyEdit(
    IREntity::EntityId voxelSetEntity,
    C_VoxelSetNew &set,
    ivec3 localIdx,
    std::size_t flat,
    bool place,
    Color placeColor,
    std::uint8_t boneId = 0
) {
    UndoEdit edit{voxelSetEntity, localIdx, set.voxels_[flat]};
    g_editor.pendingStroke_.edits_.push_back(edit);
    if (place) {
        set.voxels_[flat].color_ = placeColor;
        set.voxels_[flat].bone_id_ = boneId;
        set.voxels_[flat].layer_id_ = g_layerManager.activeLayerId();
        // Keep hidden when placed onto a currently-hidden layer.
        if (g_layerManager.isVisible(set.voxels_[flat].layer_id_))
            set.voxels_[flat].activate();
        else
            set.voxels_[flat].deactivate();
    } else {
        set.voxels_[flat].deactivate();
        set.voxels_[flat].layer_id_ = 0;
    }
}

void commitStroke() {
    if (g_editor.pendingStroke_.edits_.empty()) {
        return;
    }
    g_editor.undoTotalBytes_ += g_editor.pendingStroke_.byteSize();
    g_editor.undoRecords_.push_back(std::move(g_editor.pendingStroke_));
    g_editor.pendingStroke_.edits_.clear();
    g_editor.pendingStroke_.edits_.reserve(kUndoStrokeReserve);

    // Whole-stroke eviction from the front (D3b). Per-record eviction
    // would split a stroke; Ctrl-Z partway through a half-evicted
    // stroke would only restore part of it.
    while (g_editor.undoTotalBytes_ > kUndoByteBudget && !g_editor.undoRecords_.empty()) {
        g_editor.undoTotalBytes_ -= g_editor.undoRecords_.front().byteSize();
        g_editor.undoRecords_.pop_front();
    }
    if (g_sceneVoxelSetEntity != IREntity::kNullEntity) {
        auto &set = IREntity::getComponent<C_VoxelSetNew>(g_sceneVoxelSetEntity);
        set.resyncAfterRawEdits();
    }
}

void undoOne() {
    if (g_editor.undoRecords_.empty()) {
        return;
    }
    UndoRecord rec = std::move(g_editor.undoRecords_.back());
    g_editor.undoRecords_.pop_back();
    g_editor.undoTotalBytes_ -= rec.byteSize();
    // Replay in reverse so overlapping edits inside a stroke restore
    // in last-write-wins order — same property the forward edit chain
    // produced when authoring. Voxel-set entities are allocated once
    // in initEntities and live for the session; there is no teardown
    // path, so the getComponent lookup below assumes the set is alive
    // without an extra liveness check. A future refactor that adds
    // entity removal must guard this loop.
    std::vector<IREntity::EntityId> touchedSets;
    for (auto it = rec.edits_.rbegin(); it != rec.edits_.rend(); ++it) {
        // editable set lives for the session — no teardown path
        auto &set = IREntity::getComponent<C_VoxelSetNew>(it->voxelSet_);
        const std::size_t flat =
            static_cast<std::size_t>(IRMath::index3DtoIndex1D(it->localIdx_, set.size_));
        if (flat < set.voxels_.size()) {
            set.voxels_[flat] = it->prev_;
        }
        if (std::find(touchedSets.begin(), touchedSets.end(), it->voxelSet_) == touchedSets.end()) {
            touchedSets.push_back(it->voxelSet_);
        }
    }
    for (auto id : touchedSets) {
        auto &set = IREntity::getComponent<C_VoxelSetNew>(id);
        set.resyncAfterRawEdits();
    }
}

// Computes the local index inside `set` for a world voxel position.
// Returns true and writes `outLocal`/`outFlat` if the target lies in
// the set's bounds; false otherwise.
bool worldVoxelToLocal(
    const C_VoxelSetNew &set,
    const C_WorldTransform &worldTransform,
    ivec3 worldVoxel,
    ivec3 &outLocal,
    std::size_t &outFlat
) {
    if (set.numVoxels_ <= 0 || set.voxels_.empty()) {
        return false;
    }
    const ivec3 origin = IRMath::roundVec3HalfUp(worldTransform.translation_);
    outLocal = worldVoxel - origin;
    if (outLocal.x < 0 || outLocal.x >= set.size_.x || outLocal.y < 0 ||
        outLocal.y >= set.size_.y || outLocal.z < 0 || outLocal.z >= set.size_.z) {
        return false;
    }
    outFlat = static_cast<std::size_t>(IRMath::index3DtoIndex1D(outLocal, set.size_));
    return outFlat < set.voxels_.size();
}

// Fill all voxels in the AABB [worldA, worldB] (inclusive) inside `set`.
void applyFillAABB(
    IREntity::EntityId entity,
    C_VoxelSetNew &set,
    const C_WorldTransform &gpos,
    ivec3 worldA,
    ivec3 worldB,
    bool place,
    Color color,
    std::uint8_t boneId = 0
) {
    const ivec3 lo{
        IRMath::min(worldA.x, worldB.x),
        IRMath::min(worldA.y, worldB.y),
        IRMath::min(worldA.z, worldB.z)
    };
    const ivec3 hi{
        IRMath::max(worldA.x, worldB.x),
        IRMath::max(worldA.y, worldB.y),
        IRMath::max(worldA.z, worldB.z)
    };
    IRMath::iterateAABB(lo, hi, [&](int x, int y, int z) {
        ivec3 local{};
        std::size_t flat = 0;
        if (worldVoxelToLocal(set, gpos, {x, y, z}, local, flat))
            applyEdit(entity, set, local, flat, place, color, boneId);
    });
}

// Fill voxels along the dominant axis between worldA and worldB.
void applyFillLine(
    IREntity::EntityId entity,
    C_VoxelSetNew &set,
    const C_WorldTransform &gpos,
    ivec3 worldA,
    ivec3 worldB,
    bool place,
    Color color,
    std::uint8_t boneId = 0
) {
    const ivec3 delta = worldB - worldA;
    const int dx = IRMath::abs(delta.x);
    const int dy = IRMath::abs(delta.y);
    const int dz = IRMath::abs(delta.z);
    int axis = 0;
    int steps = dx;
    if (dy > steps) {
        axis = 1;
        steps = dy;
    }
    if (dz > steps) {
        axis = 2;
        steps = dz;
    }
    const int dirX = delta.x > 0 ? 1 : -1;
    const int dirY = delta.y > 0 ? 1 : -1;
    const int dirZ = delta.z > 0 ? 1 : -1;
    for (int i = 0; i <= steps; ++i) {
        ivec3 pos = worldA;
        if (axis == 0)
            pos.x += dirX * i;
        else if (axis == 1)
            pos.y += dirY * i;
        else
            pos.z += dirZ * i;
        ivec3 local{};
        std::size_t flat = 0;
        if (worldVoxelToLocal(set, gpos, pos, local, flat))
            applyEdit(entity, set, local, flat, place, color, boneId);
    }
}

// Flood-fill all cells in the axis-plane of `faceNormal` starting from `worldHit`.
// 4-connected BFS restricted to the plane (fixedAxis = the normal's non-zero axis).
void applyFillFace(
    IREntity::EntityId entity,
    C_VoxelSetNew &set,
    const C_WorldTransform &gpos,
    ivec3 worldHit,
    ivec3 faceNormal,
    bool place,
    Color color,
    std::uint8_t boneId = 0
) {
    int fixedAxis = -1;
    if (faceNormal.x != 0)
        fixedAxis = 0;
    else if (faceNormal.y != 0)
        fixedAxis = 1;
    else if (faceNormal.z != 0)
        fixedAxis = 2;
    if (fixedAxis < 0)
        return;

    const ivec3 origin = IRMath::roundVec3HalfUp(gpos.translation_);
    const ivec3 startLocal = worldHit - origin;
    if (startLocal.x < 0 || startLocal.x >= set.size_.x || startLocal.y < 0 ||
        startLocal.y >= set.size_.y || startLocal.z < 0 || startLocal.z >= set.size_.z)
        return;

    const int totalCells = set.size_.x * set.size_.y * set.size_.z;
    std::vector<bool> visited(static_cast<std::size_t>(totalCells), false);

    // 4-connected neighbor offsets in the plane perpendicular to fixedAxis
    auto [dim0, dim1] = IRMath::perpendicularAxes(fixedAxis);
    ivec3 step0{0, 0, 0};
    ivec3 step1{0, 0, 0};
    step0[dim0] = 1;
    step1[dim1] = 1;
    const ivec3 neighborSteps[4] = {step0, -step0, step1, -step1};

    const int startFlat = IRMath::index3DtoIndex1D(startLocal, set.size_);
    if (startFlat < 0 || static_cast<std::size_t>(startFlat) >= set.voxels_.size())
        return;
    visited[static_cast<std::size_t>(startFlat)] = true;

    std::queue<ivec3> q;
    q.push(startLocal);
    while (!q.empty()) {
        const ivec3 cur = q.front();
        q.pop();
        const int flat = IRMath::index3DtoIndex1D(cur, set.size_);
        if (flat < 0 || static_cast<std::size_t>(flat) >= set.voxels_.size())
            continue;
        applyEdit(entity, set, cur, static_cast<std::size_t>(flat), place, color, boneId);
        for (const auto &step : neighborSteps) {
            const ivec3 nb = cur + step;
            if (nb.x < 0 || nb.x >= set.size_.x || nb.y < 0 || nb.y >= set.size_.y || nb.z < 0 ||
                nb.z >= set.size_.z)
                continue;
            const int nbFlat = IRMath::index3DtoIndex1D(nb, set.size_);
            if (nbFlat < 0 || static_cast<std::size_t>(nbFlat) >= visited.size())
                continue;
            if (visited[static_cast<std::size_t>(nbFlat)])
                continue;
            visited[static_cast<std::size_t>(nbFlat)] = true;
            q.push(nb);
        }
    }
}

// CPU SDF voxel bake — fill every voxel in `set` whose SDF value for
// `shapeType`/`sdfParams` is ≤ kSurfaceThreshold. The shape is centered on
// the voxel set; the SDF math is batched into `IRMath::SDF::evaluateGrid`,
// so this function only owns the placement decision. Always produces
// DENSE output (no SHAPES chunk).
void applyFillSDF(
    IREntity::EntityId entity,
    C_VoxelSetNew &set,
    IRMath::SDF::ShapeType shapeType,
    vec4 sdfParams,
    bool place,
    Color color
) {
    const std::size_t total = static_cast<std::size_t>(set.size_.x) *
                              static_cast<std::size_t>(set.size_.y) *
                              static_cast<std::size_t>(set.size_.z);
    std::vector<float> distances(total);
    IRMath::SDF::evaluateGrid(set.size_, shapeType, sdfParams, distances);
    IRMath::iterateAABB({0, 0, 0}, set.size_ - ivec3(1), [&](int x, int y, int z) {
        const ivec3 local{x, y, z};
        const std::size_t flat =
            static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, set.size_));
        if (distances[flat] > IRMath::SDF::kSurfaceThreshold)
            return;
        if (flat < set.voxels_.size())
            applyEdit(entity, set, local, flat, place, color);
    });
}

// Update the ghost shape entity to visualize the fill region during drag.
// Uses AABB bounds for box fill, or the dominant-axis extent for line fill.
void updateGhostShape(ivec3 worldA, ivec3 worldB, bool lineMode) {
    if (g_fillTool.ghostEntity_ == IREntity::kNullEntity)
        return;
    auto &ghost = IREntity::getComponent<C_ShapeDescriptor>(g_fillTool.ghostEntity_);
    auto &ghostTransform = IREntity::getComponent<C_LocalTransform>(g_fillTool.ghostEntity_);
    ghost.flags_ = IRMath::SDF::SHAPE_FLAG_VISIBLE | IRMath::SDF::SHAPE_FLAG_HOLLOW |
                   IRMath::SDF::SHAPE_FLAG_XRAY_OCCLUDED;

    ivec3 lo{};
    ivec3 hi{};
    if (lineMode) {
        const ivec3 delta = worldB - worldA;
        const int dx = IRMath::abs(delta.x);
        const int dy = IRMath::abs(delta.y);
        const int dz = IRMath::abs(delta.z);
        ivec3 lineEnd = worldA;
        if (dx >= dy && dx >= dz)
            lineEnd.x = worldB.x;
        else if (dy >= dz)
            lineEnd.y = worldB.y;
        else
            lineEnd.z = worldB.z;
        lo = ivec3{
            IRMath::min(worldA.x, lineEnd.x),
            IRMath::min(worldA.y, lineEnd.y),
            IRMath::min(worldA.z, lineEnd.z)
        };
        hi = ivec3{
            IRMath::max(worldA.x, lineEnd.x),
            IRMath::max(worldA.y, lineEnd.y),
            IRMath::max(worldA.z, lineEnd.z)
        };
    } else {
        lo = ivec3{
            IRMath::min(worldA.x, worldB.x),
            IRMath::min(worldA.y, worldB.y),
            IRMath::min(worldA.z, worldB.z)
        };
        hi = ivec3{
            IRMath::max(worldA.x, worldB.x),
            IRMath::max(worldA.y, worldB.y),
            IRMath::max(worldA.z, worldB.z)
        };
    }
    const vec3 halfExt = vec3(hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1) * 0.5f;
    ghostTransform.translation_ = vec3(lo) + halfExt;
    ghost.params_ = vec4(halfExt.x, halfExt.y, halfExt.z, 0.0f);
}

// Loft mask cell colors. Painted onto the GUI canvas by
// IRRender::drawMaskGridOntoCanvas in the EditorLoftRender system.
constexpr Color kLoftCellOn{180, 220, 180, 230};
constexpr Color kLoftCellOff{35, 38, 48, 220};

// Place voxels in the editable set wherever both loft masks agree (CSG
// intersection). Works entirely in local voxel indices: mask[x + z*sizeX]
// is true when the front (XZ) profile includes column x at height z, and
// mask[y + z*sizeY] when the side (YZ) profile includes column y at z.
void applyLoft(Color color) {
    if (g_editor.editableVoxelSet_ == IREntity::kNullEntity)
        return;
    auto &set = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
    const int sx = set.size_.x;
    const int sy = set.size_.y;
    const int sz = set.size_.z;
    if (static_cast<int>(g_loftTool.maskXZ_.size()) < sx * sz)
        return;
    if (static_cast<int>(g_loftTool.maskYZ_.size()) < sy * sz)
        return;
    IRMath::apply3DMaskIntersection(
        g_loftTool.maskXZ_,
        {sx, sz},
        g_loftTool.maskYZ_,
        sy,
        [&](int x, int y, int z) {
            const int flat = IRMath::index3DtoIndex1D({x, y, z}, set.size_);
            if (flat < 0 || static_cast<std::size_t>(flat) >= set.voxels_.size())
                return;
            applyEdit(
                g_editor.editableVoxelSet_,
                set,
                {x, y, z},
                static_cast<std::size_t>(flat),
                true,
                color
            );
        }
    );
    commitStroke();
}

// Copy the editable target's live voxels into frames_[idx].voxels_.
// No-op when the editable target is not yet initialized (initCommands
// runs before initEntities, so command callbacks may fire before the
// editable set exists — guard rather than crash).
void snapshotLiveToFrame(int idx) {
    if (g_editor.editableVoxelSet_ == IREntity::kNullEntity)
        return;
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
    g_anim.frames_[idx].voxels_.assign(vs.voxels_.begin(), vs.voxels_.end());
}

// Copy frames_[idx].voxels_ into the editable target's live voxels.
// A size mismatch — fresh-blank frame inserted by addBlankFrame whose
// voxels_ has never been populated — fills the live voxels with
// transparent cells instead.
void loadFrameToLive(int idx) {
    if (g_editor.editableVoxelSet_ == IREntity::kNullEntity)
        return;
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
    auto &nf = g_anim.frames_[idx];
    if (nf.voxels_.size() == vs.voxels_.size()) {
        std::copy(nf.voxels_.begin(), nf.voxels_.end(), vs.voxels_.begin());
    } else {
        C_Voxel blank{Color{0, 0, 0, 0}};
        std::fill(vs.voxels_.begin(), vs.voxels_.end(), blank);
    }
}

// Snapshot the live voxels into the active frame, then load frame
// `frameIndex` into the live target. Out-of-range and same-frame are
// no-ops. Swaps the per-frame undo hot slot so undo (Ctrl-Z) only
// reaches into the history of the active frame.
void switchToFrame(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= g_anim.frameCount())
        return;
    if (frameIndex == g_anim.activeFrame_)
        return;

    const int oldFrame = g_anim.activeFrame_;

    // Save the departing frame's undo state into the cold slot.
    if (oldFrame < static_cast<int>(g_editor.perFrameUndoStacks_.size())) {
        g_editor.perFrameUndoStacks_[oldFrame] = std::move(g_editor.undoRecords_);
        g_editor.perFrameUndoBytes_[oldFrame] = g_editor.undoTotalBytes_;
    }

    snapshotLiveToFrame(oldFrame);
    g_anim.activeFrame_ = frameIndex;
    loadFrameToLive(frameIndex);

    // Restore the arriving frame's undo state into the hot slot.
    g_editor.undoRecords_ = {};
    g_editor.undoTotalBytes_ = 0;
    if (frameIndex < static_cast<int>(g_editor.perFrameUndoStacks_.size())) {
        g_editor.undoRecords_ = std::move(g_editor.perFrameUndoStacks_[frameIndex]);
        g_editor.undoTotalBytes_ = g_editor.perFrameUndoBytes_[frameIndex];
        g_editor.perFrameUndoStacks_[frameIndex] = {};
        g_editor.perFrameUndoBytes_[frameIndex] = 0;
    }

    // Mirror the new frame index onto the scrubber widget (keyboard nav).
    if (g_scrubberSlider != IREntity::kNullEntity) {
        IRPrefab::Widget::setSliderValue(g_scrubberSlider, static_cast<float>(frameIndex));
    }

    IR_LOG_INFO(
        "Frame: %d / %d  [%s  %.0f FPS]",
        g_anim.activeFrame_ + 1,
        g_anim.frameCount(),
        g_anim.playing_ ? "PLAYING" : "PAUSED",
        g_anim.fps_
    );
}

// --- F-2.5 (#1604) skeletal joint authoring + FK posing (#1610) -------------
//
// One rig per editor session: a rig-root entity carrying C_Skeleton, with
// joint entities parented under it via CHILD_OF. Each joint carries C_Joint
// and the engine's C_LocalTransform; an orange JOINT_MARKER sphere
// (IRPrefab::Gizmo::createJointMarker), a translate gizmo for placement, and
// a rotate gizmo for FK posing — all anchored to the joint itself. The index
// of a joint in C_Skeleton.joints_ IS its bone_id; bindPose_ is kept parallel.
// The "active" joint is the parent for the next add (B); R starts a fresh
// chain off the rig root. Joint selection / reparent-to-any-joint / tree
// panel land in #1607.
//
// Placement vs posing (#1610): a TRANSLATE_ARROW drag on a joint is
// authoring — bindPose_ recaptures at gesture end so the bind tracks the
// authored rest. A ROTATE_RING drag is FK posing — GIZMO_DRAG writes the
// joint's C_LocalTransform.rotation_, PROPAGATE_TRANSFORM composes the
// chain, and UPDATE_JOINT_MATRICES skins the rig's voxels live; the bind is
// deliberately NOT recaptured, so the deformation stays visible. T captures
// the current pose as the new bind explicitly (skin matrices → identity).

// Local offset of each newly-added joint from its parent (refined by drag).
// Clears the 8-unit translate arrows (shaft 6 + head 2) so one joint's +X
// handle doesn't pierce the next joint in a default-spawned chain.
constexpr vec3 kJointSpawnLocalOffset{10.0f, 0.0f, 0.0f};
// Where a freshly-created rig root sits — clear of the editable scene
// (x,y ∈ [-8,8]) AND the perimeter gizmo-reference row (y = ±12) so the
// starter chain's handles don't overlap the showcase gizmos' hover targets,
// while staying inside the lit band around the scene (further out the
// sun-shadow / light volume coverage ends and markers read near-black).
constexpr vec3 kJointRigOrigin{-12.0f, 16.0f, -4.0f};

struct JointToolState {
    bool active_ = false; // J toggles authoring mode
    IREntity::EntityId rigRoot_ = IREntity::kNullEntity;
    int activeJointIdx_ = -1;    // index into joints_ (-1 = rig root)
    std::vector<int> parentIdx_; // parallel to joints_; -1 = rig root
    bool bindPoseRecaptured_ =
        false; // cleared each beginTick; prevents O(joints×archetypes) redundant recompute
    // GIZMO_DRAG's dragHandle_ as of last frame — the bind-sync system
    // edge-detects the drag release (handle → kNullEntity) against this.
    IREntity::EntityId lastDragHandle_ = IREntity::kNullEntity;
};
JointToolState g_jointTool;

// Ensures the session's rig-root entity (the C_Skeleton owner) exists,
// creating it at kJointRigOrigin on first use. Joints parent under it.
IREntity::EntityId ensureRigRoot() {
    if (g_jointTool.rigRoot_ == IREntity::kNullEntity) {
        g_jointTool.rigRoot_ =
            IREntity::createEntity(C_LocalTransform{kJointRigOrigin}, C_Skeleton{});
    }
    return g_jointTool.rigRoot_;
}

// Recomputes C_Skeleton.bindPose_ from each joint's live C_LocalTransform,
// folding the parent chain with the same sqtCompose convention
// SYSTEM_PROPAGATE_TRANSFORM uses — so a joint left at rest has
// C_WorldTransform == bindPose_[i] and IRPrefab::Skeleton::skinMatrix returns
// identity at the bind pose. joints_ is in creation order, so a parent's bind
// slot is always resolved before its children's.
void recomputeJointBindPose() {
    if (g_jointTool.rigRoot_ == IREntity::kNullEntity)
        return;
    auto &skeleton = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_);
    IR_ASSERT(
        g_jointTool.parentIdx_.size() == skeleton.joints_.size(),
        "parentIdx_ / joints_ size mismatch"
    );
    const std::size_t count = skeleton.joints_.size();
    skeleton.bindPose_.assign(count, IRMath::SQT{});
    for (std::size_t i = 0; i < count; ++i) {
        const auto &local = IREntity::getComponent<C_LocalTransform>(skeleton.joints_[i]);
        const IRMath::SQT localSqt{local.scale_, local.rotation_, local.translation_};
        const int parent = g_jointTool.parentIdx_[i];
        skeleton.bindPose_[i] =
            (parent < 0) ? localSqt : IRMath::sqtCompose(skeleton.bindPose_[parent], localSqt);
    }
}

// True when `entity` is one of the session rig's joints (the drag anchor of
// a per-joint gizmo handle). O(joints) scan — called once per drag release.
bool isRigJoint(IREntity::EntityId entity) {
    if (entity == IREntity::kNullEntity || g_jointTool.rigRoot_ == IREntity::kNullEntity)
        return false;
    const auto &joints = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_).joints_;
    return std::find(joints.begin(), joints.end(), entity) != joints.end();
}

// "Set current pose as bind" (#1610): capture every joint's live local
// transform chain into bindPose_. The posed shape becomes the new rest —
// skin matrices return to identity and the rig's voxels relax in place.
void setCurrentPoseAsBind() {
    if (g_jointTool.rigRoot_ == IREntity::kNullEntity)
        return;
    recomputeJointBindPose();
    IR_LOG_INFO("Bind pose captured from the current pose (skin matrices -> identity).");
}

// Spawns one joint parented to the active joint (or the rig root when none /
// after R), appends it to C_Skeleton.joints_ (its index is the bone_id),
// refreshes the parallel bind pose, and makes it the new active joint. The
// joint renders as an orange sphere and carries a translate gizmo anchored to
// itself for placement.
IREntity::EntityId addJointAuthored() {
    const IREntity::EntityId rigRoot = ensureRigRoot();
    const int parentIdx = g_jointTool.activeJointIdx_;
    const IREntity::EntityId parentEntity =
        (parentIdx < 0) ? rigRoot : IREntity::getComponent<C_Skeleton>(rigRoot).joints_[parentIdx];

    // Structural changes — spawn the joint and its marker/gizmo children
    // first, then re-fetch C_Skeleton so the stored reference can't dangle if
    // any createEntity reshuffled the rig root's archetype storage.
    const IREntity::EntityId joint =
        IREntity::createEntity(C_LocalTransform{kJointSpawnLocalOffset}, C_Joint{});
    IREntity::setParent(joint, parentEntity);
    // Orange JOINT_MARKER sphere (hover-highlight + xray silhouette + the
    // screen-space size pass), per-joint translate arrows for placement, and
    // per-joint rotate rings for FK posing (#1610) — every drag mutates the
    // joint's own C_LocalTransform (translation vs rotation by handle kind).
    IRPrefab::Gizmo::createJointMarker(joint);
    IRPrefab::Gizmo::createTranslateGizmoForAnchor(joint);
    IRPrefab::Gizmo::createRotateGizmoForAnchor(joint);

    auto &skeleton = IREntity::getComponent<C_Skeleton>(rigRoot);
    skeleton.joints_.push_back(joint);
    g_jointTool.parentIdx_.push_back(parentIdx);
    g_jointTool.activeJointIdx_ = static_cast<int>(skeleton.joints_.size()) - 1;
    recomputeJointBindPose();

    IR_LOG_INFO("Joint added: bone {} (parent bone {})", g_jointTool.activeJointIdx_, parentIdx);
    return joint;
}

// Starts a fresh bone chain: the next B-add parents to the rig root rather
// than chaining off the last-added joint. (Reparent-to-any-joint is #1607.)
void resetJointChain() {
    g_jointTool.activeJointIdx_ = -1;
    IR_LOG_INFO("Joint chain reset — next joint parents to the rig root.");
}

// Author a short starter chain so the feature is visible on launch and in
// auto-screenshots, mirroring the perimeter gizmo references in initEntities.
//
// #1610 also rigs the chain with a skinned voxel bar (the FK verification
// vehicle): a 31×3×3 bar on the rig root, painted one bone per third by
// nearest joint. UPDATE_JOINT_MATRICES allocates the skeleton's slot block on
// its first tick and auto-seeds the per-voxel bone→slot indices (#1605), so
// dragging a rotate ring on a mid-chain joint visibly bends the bar live.
void seedDemoSkeleton() {
    const IREntity::EntityId rigRoot = ensureRigRoot();
    g_jointTool.activeJointIdx_ = -1;
    addJointAuthored();
    addJointAuthored();
    addJointAuthored();

    // Center the chain in the bar's 3×3 cross-section: the bar's local
    // coords span y,z ∈ [0..2], so lift the first joint to (10,1,1) and the
    // chained joints (+10 x each) follow on the y=1,z=1 line through the
    // bar. Re-capture the bind so the lifted chain is the rest pose.
    {
        auto &skeleton = IREntity::getComponent<C_Skeleton>(rigRoot);
        auto &firstLocal = IREntity::getComponent<C_LocalTransform>(skeleton.joints_[0]);
        firstLocal.translation_ = vec3(10.0f, 1.0f, 1.0f);
        recomputeJointBindPose();
    }

    // The skinned bar: local x ∈ [0..30] spans the joints at x = 10/20/30.
    // Painted per-segment colors make each bone's span legible while posing.
    IREntity::setComponent(rigRoot, C_VoxelSetNew{ivec3(31, 3, 3), Color{210, 160, 110, 255}});
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(rigRoot);
    const auto &skeleton = IREntity::getComponent<C_Skeleton>(rigRoot);
    constexpr Color kBoneSegmentColors[] = {
        Color{220, 130, 110, 255},
        Color{130, 200, 120, 255},
        Color{120, 150, 220, 255},
    };
    for (std::size_t i = 0; i < voxelSet.voxels_.size(); ++i) {
        // Nearest joint along the chain axis owns the voxel.
        const float x = voxelSet.positions_[i].pos_.x;
        std::uint8_t bone = 0;
        float best = IRMath::abs(x - skeleton.bindPose_[0].translation_.x);
        for (std::uint8_t j = 1; j < skeleton.joints_.size(); ++j) {
            const float d = IRMath::abs(x - skeleton.bindPose_[j].translation_.x);
            if (d < best) {
                best = d;
                bone = j;
            }
        }
        voxelSet.voxels_[i].bone_id_ = bone;
        voxelSet.voxels_[i].color_ = kBoneSegmentColors[bone];
    }
}

} // namespace

} // namespace IRVoxelEditor

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: voxel_editor");
    IR_LOG_INFO("  Left-drag: AABB box-fill between drag-start and drag-end");
    IR_LOG_INFO("  Shift + left-drag: line-fill along dominant axis");
    IR_LOG_INFO("  Ctrl + left-click: face-fill (flood-fill axis-plane of hit face)");
    IR_LOG_INFO("  Left-click (no drag): place single voxel adjacent to hit face");
    IR_LOG_INFO("  Right-click: erase hit voxel (drag still rotates camera)");
    IR_LOG_INFO("  V: toggle erase-fill mode (left-click place/box/line/face gestures erase)");
    IR_LOG_INFO("  Escape: cancel active drag without committing");
    IR_LOG_INFO("  Middle-drag: pan camera");
    IR_LOG_INFO("  Scroll: zoom in/out");
    IR_LOG_INFO("  Q/E: snap-rotate 90 deg CCW/CW");
    IR_LOG_INFO("  Space: re-center + reset yaw");
    IR_LOG_INFO("  Ctrl+Z: undo last stroke");
    IR_LOG_INFO("  X/Y/Z: toggle X/Y/Z mirror symmetry");
    IR_LOG_INFO("  Left/Right arrow: previous/next frame");
    IR_LOG_INFO("  P: play/pause  A: add blank frame  D: duplicate  Backspace: delete frame");
    IR_LOG_INFO("  L: toggle loop mode (LOOP / PING-PONG)");
    IR_LOG_INFO("  F: toggle loft mode (paint XZ+YZ profiles)  Enter: stamp  C: clear masks");
    IR_LOG_INFO("  J: toggle joint authoring  B: add joint to chain  R: start new chain");
    IR_LOG_INFO("  N: toggle bone-paint mode (click swatch in BONE panel to pick bone)");
    IR_LOG_INFO("  Joint arrows place (re-binds at release); rings FK-pose (live deform)");
    IR_LOG_INFO("  T: set current pose as bind (joint mode)");
    // Editable grid dims (#766 Part 2): the ant needs 20³, the tree ~26 tall.
    // Register before init (which owns the parse); read back + derive the origin
    // after. Omitted / non-positive dims keep the historical 16³ scene.
    IREngine::args()
        .numbers("--scene-size", "editable voxel grid dims: W H D (default 16 16 16)", 3);
    IREngine::init(argc, argv);
    {
        const std::vector<float> &dims = IREngine::args().getFloats("--scene-size");
        if (dims.size() == 3 && dims[0] >= 1.0f && dims[1] >= 1.0f && dims[2] >= 1.0f) {
            IRVoxelEditor::g_editableSceneSize = ivec3(
                static_cast<int>(dims[0]),
                static_cast<int>(dims[1]),
                static_cast<int>(dims[2])
            );
            IRVoxelEditor::g_editableSceneOrigin =
                IRVoxelEditor::deriveSceneOrigin(IRVoxelEditor::g_editableSceneSize);
        }
        IR_LOG_INFO(
            "Editor scene size: {}x{}x{} (origin {},{},{})",
            IRVoxelEditor::g_editableSceneSize.x,
            IRVoxelEditor::g_editableSceneSize.y,
            IRVoxelEditor::g_editableSceneSize.z,
            IRVoxelEditor::g_editableSceneOrigin.x,
            IRVoxelEditor::g_editableSceneOrigin.y,
            IRVoxelEditor::g_editableSceneOrigin.z
        );
    }
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

static void updateSwatchSelection(const std::vector<IREntity::EntityId> &swatches, int &activeIdx) {
    const int n = static_cast<int>(swatches.size());
    for (int i = 0; i < n; ++i) {
        if (IRPrefab::Widget::wasClicked(swatches[i])) {
            activeIdx = i;
            break;
        }
    }
    for (int i = 0; i < n; ++i) {
        IRPrefab::Widget::setColorSwatchSelected(swatches[i], i == activeIdx);
    }
}

void initSystems() {
    using IRVoxelEditor::RotateParams;

    // Loft-mask render: draws the XZ and YZ mask grids onto the GUI canvas.
    // Runs in the RENDER pipeline after TEXT_TO_TRIXEL (canvas clear) so
    // the grids paint over the cleared canvas. Pixel-packing + texture
    // upload live in IRRender::drawMaskGridOntoCanvas (mask_grid_painter.hpp);
    // scratch_ is grown to the largest grid size and reused across frames.
    struct LoftRenderParams {
        C_TriangleCanvasTextures *canvas_ = nullptr;
        IRRender::MaskGridPaintScratch scratch_;
        std::vector<IRRender::GlyphDrawCommand> textCmds_;
    };
    auto loftRenderData = std::make_unique<LoftRenderParams>();
    auto *lrp = loftRenderData.get();
    auto loftRenderSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorLoftRender",
        [](const C_GuiElement &) {},
        [lrp]() {
            lrp->canvas_ =
                &IREntity::getComponent<C_TriangleCanvasTextures>(IRRender::getCanvas("gui"));
        },
        [lrp]() {
            if (!IRVoxelEditor::g_loftTool.active_ || !lrp->canvas_)
                return;
            auto &loft = IRVoxelEditor::g_loftTool;
            const int sx = IRVoxelEditor::g_editableSceneSize.x;
            const int sy = IRVoxelEditor::g_editableSceneSize.y;
            const int sz = IRVoxelEditor::g_editableSceneSize.z;
            IRRender::drawMaskGridOntoCanvas(
                *lrp->canvas_,
                loft.maskXZ_,
                ivec2(sx, sz),
                IRVoxelEditor::kLoftGridXZPos,
                IRVoxelEditor::kLoftCellPx,
                IRVoxelEditor::kLoftCellOn,
                IRVoxelEditor::kLoftCellOff,
                IRRender::kWidgetBackgroundDistance,
                lrp->scratch_
            );
            IRRender::drawMaskGridOntoCanvas(
                *lrp->canvas_,
                loft.maskYZ_,
                ivec2(sy, sz),
                IRVoxelEditor::kLoftGridYZPos,
                IRVoxelEditor::kLoftCellPx,
                IRVoxelEditor::kLoftCellOn,
                IRVoxelEditor::kLoftCellOff,
                IRRender::kWidgetBackgroundDistance,
                lrp->scratch_
            );
            const int gridH = sz * IRVoxelEditor::kLoftCellPx;
            const ivec2 canvasSize = lrp->canvas_->size_;
            IRPrefab::GuiText::queueGuiText(
                lrp->textCmds_,
                "XZ",
                IRVoxelEditor::kLoftGridXZPos + ivec2(0, -12),
                canvasSize,
                Color{200, 220, 200, 220},
                1
            );
            IRPrefab::GuiText::queueGuiText(
                lrp->textCmds_,
                "YZ",
                IRVoxelEditor::kLoftGridYZPos + ivec2(0, -12),
                canvasSize,
                Color{200, 220, 200, 220},
                1
            );
            IRPrefab::GuiText::queueGuiText(
                lrp->textCmds_,
                "LOFT  Shift=sym  C=clear  Enter=stamp  F=exit",
                ivec2(IRVoxelEditor::kLoftGridXZPos.x, IRVoxelEditor::kLoftGridXZPos.y + gridH + 4),
                canvasSize,
                Color{200, 200, 200, 180},
                1
            );
            // Inline dispatch (not deferred to endTick like the widget
            // render systems): the loft overlay is a single-canvas system
            // that runs its whole render once per frame in this tick body,
            // so queuing and dispatching here is equivalent to the deferred
            // pattern — there is no second tick that would append more glyphs
            // before the dispatch.
            IRPrefab::GuiText::dispatchGuiText(lrp->textCmds_);
        }
    );
    IRSystem::setSystemParams(loftRenderSystem, std::move(loftRenderData));

    // Hover-help render: draws the most-specific hovered widget's help text into
    // the HELP panel. Registered last among the GUI renders so the text lands
    // over the HELP panel background; reuses the batched GUI-text path (#1774).
    struct HelpRenderParams {
        C_TriangleCanvasTextures *canvas_ = nullptr;
        std::vector<IRRender::GlyphDrawCommand> textCmds_;
    };
    auto helpRenderData = std::make_unique<HelpRenderParams>();
    auto *hrp = helpRenderData.get();
    auto helpRenderSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorHelpRender",
        [](const C_GuiElement &) {},
        [hrp]() {
            hrp->canvas_ =
                &IREntity::getComponent<C_TriangleCanvasTextures>(IRRender::getCanvas("gui"));
        },
        [hrp]() {
            if (hrp->canvas_ == nullptr)
                return;
            const char *help = nullptr;
            int bestArea = 0;
            for (const auto &entry : IRVoxelEditor::g_helpEntries) {
                auto hitbox = IREntity::getComponentOptional<C_HitBox2DGui>(entry.widget_);
                if (!hitbox.has_value() || !(*hitbox)->hovered_)
                    continue;
                const int area = (*hitbox)->size_.x * (*hitbox)->size_.y;
                if (help == nullptr || area < bestArea) {
                    bestArea = area;
                    help = entry.text_;
                }
            }
            const char *shown = (help != nullptr) ? help : "Hover a panel or control for help.";
            IRPrefab::GuiText::queueGuiText(
                hrp->textCmds_,
                shown,
                IRVoxelEditor::kHelpPanelPos + ivec2(6, 22),
                hrp->canvas_->size_,
                Color{180, 200, 220, 255},
                1,
                IRComponents::TextAlignH::LEFT,
                IRComponents::TextAlignV::TOP,
                0,
                0,
                IRVoxelEditor::kHelpPanelSize.x - 12
            );
            IRPrefab::GuiText::dispatchGuiText(hrp->textCmds_);
        }
    );
    IRSystem::setSystemParams(helpRenderSystem, std::move(helpRenderData));

    // Loft-mask input: detects left-click / drag over the XZ and YZ grid
    // panels and toggles or paints mask cells. Toggle direction is fixed
    // at the first PRESSED event for the duration of the drag stroke; Shift
    // mirrors each painted cell horizontally within the same mask.
    struct LoftInputParams {
        vec2 mouseGuiTrixel_ = vec2(0.0f);
        bool painting_ = false;
        bool paintVal_ = true;
    };
    auto loftInputData = std::make_unique<LoftInputParams>();
    auto *lip = loftInputData.get();
    auto loftInputSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorLoftInput",
        [](const C_GuiElement &) {},
        [lip]() { lip->mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels(); },
        [lip]() {
            if (!IRVoxelEditor::g_loftTool.active_)
                return;

            const bool leftPressed =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
            const bool leftHeld =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::HELD);
            const bool leftReleased =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::RELEASED);

            if (leftReleased) {
                lip->painting_ = false;
                return;
            }
            if (!leftPressed && !leftHeld)
                return;
            if (!lip->painting_ && leftHeld)
                return;

            const ivec2 mouseGui(
                static_cast<int>(lip->mouseGuiTrixel_.x),
                static_cast<int>(lip->mouseGuiTrixel_.y)
            );
            const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift, 0u);
            const int sx = IRVoxelEditor::g_editableSceneSize.x;
            const int sy = IRVoxelEditor::g_editableSceneSize.y;
            const int sz = IRVoxelEditor::g_editableSceneSize.z;
            const int cell = IRVoxelEditor::kLoftCellPx;

            auto paintCell = [&](std::vector<bool> &mask, ivec2 cellHV, int sH) {
                const std::size_t flat = static_cast<std::size_t>(cellHV.x + cellHV.y * sH);
                if (leftPressed) {
                    lip->paintVal_ = !mask[flat];
                    lip->painting_ = true;
                }
                if (!lip->painting_)
                    return;
                mask[flat] = lip->paintVal_;
                if (shiftHeld) {
                    const int mirror = sH - 1 - cellHV.x;
                    if (mirror != cellHV.x)
                        mask[static_cast<std::size_t>(mirror + cellHV.y * sH)] = lip->paintVal_;
                }
            };

            if (auto hit = IRRender::hitTestGridCell(
                    mouseGui,
                    IRVoxelEditor::kLoftGridXZPos,
                    cell,
                    ivec2(sx, sz)
                )) {
                paintCell(IRVoxelEditor::g_loftTool.maskXZ_, *hit, sx);
                return;
            }
            if (auto hit = IRRender::hitTestGridCell(
                    mouseGui,
                    IRVoxelEditor::kLoftGridYZPos,
                    cell,
                    ivec2(sy, sz)
                )) {
                paintCell(IRVoxelEditor::g_loftTool.maskYZ_, *hit, sy);
                return;
            }

            // Click outside both grids — do not start a paint stroke.
            if (leftPressed)
                lip->painting_ = false;
        }
    );
    IRSystem::setSystemParams(loftInputSystem, std::move(loftInputData));

    // Palette swatch poller: detect which swatch fired this frame,
    // update the active index, and mirror the selection bit onto every
    // swatch so the render system can highlight the active one. Runs
    // in INPUT, AFTER WIDGET_INPUT so `fireAction_` is already set for
    // this frame's clicks. Uses a tag component as the archetype filter
    // — needs to fire even when there are zero matching entities, so we
    // use C_GuiElement (every widget carries it) and only act in
    // beginTick/endTick.
    auto paletteUpdateSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorPaletteUpdate",
        [](const C_GuiElement &) {},
        []() {
            updateSwatchSelection(
                IRVoxelEditor::g_editor.paletteSwatches_,
                IRVoxelEditor::g_editor.activeSwatchIdx_
            );
        }
    );

    // Bone-paint selector (F-2.7 / #1608). Clicking a swatch sets activeBoneIdx_
    // and reconciles the selected-bit so the renderer highlights the active bone.
    auto bonePaintUpdateSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorBonePaintUpdate",
        [](const C_GuiElement &) {},
        []() {
            updateSwatchSelection(
                IRVoxelEditor::g_bonePaint.boneSwatches_,
                IRVoxelEditor::g_bonePaint.activeBoneIdx_
            );
        }
    );

    // Place / erase / fill driver. Three left-click gestures:
    //   Ctrl + left-click       → face-fill (flood-fill the hit face's axis-plane)
    //   Shift + left-drag       → line-fill along the dominant axis
    //   left-drag (no modifier) → AABB box-fill
    //   left-click (no drag)    → single-voxel place (same as original behavior)
    // Right-click PRESSED → single-voxel erase (unchanged).
    auto placeEraseSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorPlaceErase",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            // All 3D editing is suppressed in loft mode — the loft input system
            // handles mouse events over the mask panels instead.
            if (IRVoxelEditor::g_loftTool.active_)
                return;

            if (IRVoxelEditor::g_fillModeLabel != IREntity::kNullEntity) {
                std::string status;
                const bool shiftNow = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift, 0u);
                const bool ctrlNow = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u);
                if (IRVoxelEditor::g_eraseMode) {
                    // Erase mode takes precedence over bone-paint — erasing uses
                    // no color, so the active bone is irrelevant while it is on.
                    status = ctrlNow ? "ERASE FACE" : (shiftNow ? "ERASE LINE" : "ERASE BOX");
                } else if (IRVoxelEditor::g_bonePaint.active_) {
                    status = "BONE";
                } else {
                    status = ctrlNow ? "FACE" : (shiftNow ? "LINE" : "BOX");
                }
                const auto &sym = IRVoxelEditor::g_symmetry;
                if (sym.enableX_ || sym.enableY_ || sym.enableZ_) {
                    status += " |";
                    if (sym.enableX_)
                        status += " X";
                    if (sym.enableY_)
                        status += " Y";
                    if (sym.enableZ_)
                        status += " Z";
                }
                IRPrefab::Widget::setLabelText(IRVoxelEditor::g_fillModeLabel, std::move(status));
            }

            bool overWidget = IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.palettePanel_) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_layerPanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_bakePanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_bonePaint.bonePanel_) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_skeletonPanel);
            if (!overWidget) {
                const int n = static_cast<int>(IRVoxelEditor::g_editor.paletteSwatches_.size());
                for (int i = 0; i < n; ++i) {
                    if (IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.paletteSwatches_[i])) {
                        overWidget = true;
                        break;
                    }
                }
            }
            if (!overWidget) {
                const int n = static_cast<int>(IRVoxelEditor::g_bonePaint.boneSwatches_.size());
                for (int i = 0; i < n; ++i) {
                    if (IRPrefab::Widget::isHovered(IRVoxelEditor::g_bonePaint.boneSwatches_[i])) {
                        overWidget = true;
                        break;
                    }
                }
            }

            const bool inBoneMode = IRVoxelEditor::g_bonePaint.active_;
            const std::uint8_t placeBoneId =
                inBoneMode ? static_cast<std::uint8_t>(IRVoxelEditor::g_bonePaint.activeBoneIdx_)
                           : std::uint8_t{0};
            const Color placeColor =
                inBoneMode
                    ? IRVoxelEditor::kBoneColors[IRVoxelEditor::g_bonePaint.activeBoneIdx_]
                    : IRVoxelEditor::kPaletteColors[IRVoxelEditor::g_editor.activeSwatchIdx_];

            // Right-click: single-voxel erase (bone_id_ unaffected — erase only).
            if (!overWidget &&
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::PRESSED)) {
                const auto hit = IRPrefab::Picking::castVoxelRay();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    auto &set = IREntity::getComponent<C_VoxelSetNew>(hit->entity_);
                    auto &gpos = IREntity::getComponent<C_WorldTransform>(hit->entity_);
                    ivec3 local{};
                    std::size_t flat = 0;
                    if (IRVoxelEditor::worldVoxelToLocal(set, gpos, hit->voxelPos_, local, flat)) {
                        IRVoxelEditor::applyEdit(hit->entity_, set, local, flat, false, placeColor);
                        IRVoxelEditor::commitStroke();
                    }
                }
            }

            // Ctrl + left-click: face-fill (immediate, no drag).
            const bool ctrlDown = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u);
            const bool leftPressedNow =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
            if (!overWidget && ctrlDown && leftPressedNow) {
                const auto hit = IRPrefab::Picking::castVoxelRay();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    auto &set = IREntity::getComponent<C_VoxelSetNew>(hit->entity_);
                    auto &gpos = IREntity::getComponent<C_WorldTransform>(hit->entity_);
                    // Erase mode floods the hit voxel's own face plane (removing
                    // the exposed layer); place mode floods the empty plane in
                    // front of the hit face.
                    const bool erase = IRVoxelEditor::g_eraseMode;
                    const ivec3 faceStart =
                        erase ? hit->voxelPos_ : hit->voxelPos_ + hit->faceNormal_;
                    IRVoxelEditor::applyFillFace(
                        hit->entity_,
                        set,
                        gpos,
                        faceStart,
                        hit->faceNormal_,
                        !erase,
                        placeColor,
                        placeBoneId
                    );
                    IRVoxelEditor::commitStroke();
                }
                return;
            }

            // Left drag state machine (no Ctrl): PRESSED→start, HELD→preview, RELEASED→commit.
            const bool noCtrl = !ctrlDown;
            const bool leftReleasedNow =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::RELEASED);

            if (noCtrl && !overWidget && leftPressedNow) {
                const auto hit = IRPrefab::Picking::castVoxelRay();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    // Erase drags target the hit voxels themselves; place drags
                    // target the empty cells adjacent to the hit face.
                    const ivec3 startPos = IRVoxelEditor::g_eraseMode
                                               ? hit->voxelPos_
                                               : hit->voxelPos_ + hit->faceNormal_;
                    IRVoxelEditor::g_fillTool.dragging_ = true;
                    IRVoxelEditor::g_fillTool.dragStartWorld_ = startPos;
                    IRVoxelEditor::g_fillTool.dragStartEntity_ = hit->entity_;
                    IRVoxelEditor::g_fillTool.lastEndWorld_ = startPos;
                    IRVoxelEditor::updateGhostShape(startPos, startPos, false);
                }
            }

            if (noCtrl && IRVoxelEditor::g_fillTool.dragging_ &&
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::HELD)) {
                const auto hit = IRPrefab::Picking::castVoxelRay();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    const ivec3 endPos = IRVoxelEditor::g_eraseMode
                                             ? hit->voxelPos_
                                             : hit->voxelPos_ + hit->faceNormal_;
                    IRVoxelEditor::g_fillTool.lastEndWorld_ = endPos;
                    const bool shiftHeld =
                        IRInput::checkKeyMouseModifiers(IRInput::kModifierShift, 0u);
                    IRVoxelEditor::updateGhostShape(
                        IRVoxelEditor::g_fillTool.dragStartWorld_,
                        endPos,
                        shiftHeld
                    );
                }
            }

            if (leftReleasedNow && IRVoxelEditor::g_fillTool.dragging_) {
                IRVoxelEditor::g_fillTool.dragging_ = false;
                if (IRVoxelEditor::g_fillTool.ghostEntity_ != IREntity::kNullEntity) {
                    IREntity::getComponent<C_ShapeDescriptor>(
                        IRVoxelEditor::g_fillTool.ghostEntity_
                    )
                        .flags_ = IRMath::SDF::SHAPE_FLAG_NONE;
                }
                const IREntity::EntityId targetEntity = IRVoxelEditor::g_fillTool.dragStartEntity_;
                if (targetEntity == IREntity::kNullEntity)
                    return;
                auto &set = IREntity::getComponent<C_VoxelSetNew>(targetEntity);
                auto &gpos = IREntity::getComponent<C_WorldTransform>(targetEntity);
                const ivec3 startPos = IRVoxelEditor::g_fillTool.dragStartWorld_;
                const ivec3 endPos = IRVoxelEditor::g_fillTool.lastEndWorld_;
                const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift, 0u);
                // Erase mode flips the whole drag to removal; startPos / endPos
                // were already captured in the hit-cell frame at PRESS / HELD.
                const bool place = !IRVoxelEditor::g_eraseMode;

                if (startPos == endPos) {
                    ivec3 local{};
                    std::size_t flat = 0;
                    if (IRVoxelEditor::worldVoxelToLocal(set, gpos, startPos, local, flat))
                        IRVoxelEditor::applyEdit(
                            targetEntity,
                            set,
                            local,
                            flat,
                            place,
                            placeColor,
                            placeBoneId
                        );
                } else if (shiftHeld) {
                    IRVoxelEditor::applyFillLine(
                        targetEntity,
                        set,
                        gpos,
                        startPos,
                        endPos,
                        place,
                        placeColor,
                        placeBoneId
                    );
                } else {
                    IRVoxelEditor::applyFillAABB(
                        targetEntity,
                        set,
                        gpos,
                        startPos,
                        endPos,
                        place,
                        placeColor,
                        placeBoneId
                    );
                }
                IRVoxelEditor::commitStroke();
            }
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GIZMO_SCREEN_SPACE_SIZE>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );

    // INPUT pipeline. WIDGET_INPUT writes per-widget hover/press/fire
    // state; the palette poller reads that state to set the active
    // swatch; the place/erase system reads `isHovered` to suppress
    // scene clicks under the palette. Gizmo input lands after the
    // widget chain so an over-gizmo click doesn't trip the scene-
    // edit path either.
    auto rotState = std::make_shared<RotateParams>();
    auto rotateSystem = IRSystem::createSystem<C_Camera>(
        "EditorViewportRotate",
        [](C_Camera &) {},
        [rotState]() {
            bool rightPressed =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::PRESSED);
            bool rightHeld =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::HELD);

            if (rightPressed) {
                rotState->firstRotFrame_ = true;
            }

            if (rightHeld) {
                vec2 mouse = IRInput::getMousePositionScreen();
                if (!rotState->firstRotFrame_) {
                    float deltaX = mouse.x - rotState->prevMouseX_;
                    IRPrefab::Camera::rotateYaw(deltaX * IRVoxelEditor::kRotationSensitivity);
                }
                rotState->prevMouseX_ = mouse.x;
                rotState->firstRotFrame_ = false;
            }
        }
    );

    // Scrubber + FPS sync (T-214). Runs in INPUT, after WIDGET_APPLY_SLIDER
    // so the drag value is already committed to C_WidgetSlider::currentValue_.
    // When the slider is pressed (user dragging), drives switchToFrame.
    // When not pressed, mirrors g_anim.activeFrame_ back onto the slider so
    // keyboard nav and playback keep the thumb in sync. Also pulls the FPS
    // slider value into g_anim.fps_ each tick.
    auto scrubberSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorScrubberSync",
        [](const C_GuiElement &) {},
        []() {
            if (IRVoxelEditor::g_scrubberSlider == IREntity::kNullEntity)
                return;
            auto &slider = IREntity::getComponent<C_WidgetSlider>(IRVoxelEditor::g_scrubberSlider);
            slider.maxValue_ = static_cast<float>(IRVoxelEditor::g_anim.frameCount() - 1);
            if (IRPrefab::Widget::isPressed(IRVoxelEditor::g_scrubberSlider)) {
                const int next = static_cast<int>(IRMath::round(slider.currentValue_));
                IRVoxelEditor::switchToFrame(next);
            } else {
                slider.currentValue_ = IRMath::clamp(
                    static_cast<float>(IRVoxelEditor::g_anim.activeFrame_),
                    slider.minValue_,
                    slider.maxValue_
                );
            }
            if (IRVoxelEditor::g_fpsSlider != IREntity::kNullEntity) {
                IRVoxelEditor::g_anim.fps_ =
                    IRPrefab::Widget::sliderValue(IRVoxelEditor::g_fpsSlider);
            }
        }
    );

    // Frame-based animation playback (T-214). Runs once per RENDER tick
    // in beginTick over C_Camera (the singleton camera entity), so
    // the swap lands BEFORE this frame's voxel-to-trixel stages read
    // C_VoxelSetNew::voxels_. Use the camera archetype filter because
    // we need a one-shot per-frame fire regardless of voxel-set state;
    // the per-entity tick is a no-op, all work happens in beginTick.
    // tickPlayback returns the next frame index via out-param without
    // touching g_anim.activeFrame_ — that lets switchToFrame snapshot
    // the old active frame's voxels before swapping in the new one.
    auto animPlaybackSystem = IRSystem::createSystem<C_Camera>(
        "EditorAnimPlayback",
        [](C_Camera &) {},
        []() {
            const float dt = static_cast<float>(IRTime::deltaTime(IRTime::Events::RENDER));
            int next = 0;
            if (IRVoxelEditor::tickPlayback(IRVoxelEditor::g_anim, dt, next))
                IRVoxelEditor::switchToFrame(next);
        }
    );

    // Layer panel sync (T-213). Runs in INPUT after WIDGET_APPLY_LIST and
    // WIDGET_APPLY_CHECKBOX so click state is already committed. Syncs
    // g_layerManager ↔ the LAYERS panel widgets in both directions:
    //   - list click → setActiveLayer; keyboard nav mirrors back to selection
    //   - checkbox click → toggleLayerVisibility + applyLayerVisibility
    //   - add/delete buttons → addLayer / deleteLayer with voxel migration
    //   - layer manager state always mirrored to list items and checkbox
    auto layerSyncSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorLayerSync",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            using namespace IRVoxelEditor;
            if (g_layerList == IREntity::kNullEntity)
                return;

            auto &list = IREntity::getComponent<C_WidgetList>(g_layerList);
            const auto &layers = g_layerManager.layers();

            // List click → set active layer.
            if (IRPrefab::Widget::wasClicked(g_layerList)) {
                const int sel = list.selectedIndex_;
                if (sel >= 0 && sel < static_cast<int>(layers.size()))
                    g_layerManager.setActiveLayer(layers[static_cast<std::size_t>(sel)].id_);
            }

            // Checkbox click → toggle active layer visibility + update voxels.
            if (g_layerVisCheckbox != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_layerVisCheckbox)) {
                const std::uint8_t activeId = g_layerManager.activeLayerId();
                bool nowVisible = g_layerManager.toggleLayerVisibility(activeId);
                applyLayerVisibility(activeId, nowVisible);
            }

            // Add button → new layer, auto-named, becomes active.
            if (g_layerAddBtn != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_layerAddBtn)) {
                std::uint8_t id = g_layerManager.addLayer(
                    "layer " + std::to_string(g_layerManager.layers().size())
                );
                if (id != 0)
                    g_layerManager.setActiveLayer(id);
            }

            // Delete button → migrate voxels to default, then remove layer.
            if (g_layerDelBtn != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_layerDelBtn)) {
                const std::uint8_t delId = g_layerManager.activeLayerId();
                if (delId != 0 && g_sceneVoxelSetEntity != IREntity::kNullEntity) {
                    const bool defaultVisible = g_layerManager.isVisible(0);
                    auto &set = IREntity::getComponent<C_VoxelSetNew>(g_sceneVoxelSetEntity);
                    set.editVoxels([&](int, C_Voxel &v, vec3) {
                        if (v.layer_id_ != delId)
                            return;
                        v.layer_id_ = 0;
                        if (defaultVisible)
                            v.activate();
                        else
                            v.deactivate();
                    });
                    g_layerManager.deleteLayer(delId);
                }
            }

            // Rebuild list items (names + "[H]" suffix for hidden layers).
            // Compare against the existing entry first so a clean frame
            // skips the per-layer std::string concatenation alloc.
            static constexpr const char *kHiddenSuffix = " [H]";
            static constexpr std::size_t kHiddenSuffixLen = 4; // strlen(" [H]")
            const auto &updatedLayers = g_layerManager.layers();
            if (list.items_.size() != updatedLayers.size())
                list.items_.resize(updatedLayers.size());
            for (std::size_t i = 0; i < updatedLayers.size(); ++i) {
                const auto &name = updatedLayers[i].name_;
                const bool hidden = !updatedLayers[i].visible_;
                const std::size_t suffixLen = hidden ? kHiddenSuffixLen : 0u;
                const std::string &existing = list.items_[i];
                if (existing.size() == name.size() + suffixLen &&
                    existing.compare(0, name.size(), name) == 0 &&
                    (!hidden || existing.compare(name.size(), suffixLen, kHiddenSuffix) == 0)) {
                    continue;
                }
                list.items_[i] = hidden ? name + kHiddenSuffix : name;
            }

            // Mirror active layer → list selection.
            const std::uint8_t activeId = g_layerManager.activeLayerId();
            for (int i = 0; i < static_cast<int>(updatedLayers.size()); ++i) {
                if (updatedLayers[static_cast<std::size_t>(i)].id_ == activeId) {
                    IRPrefab::Widget::setListSelectedIndex(g_layerList, i);
                    break;
                }
            }

            // Mirror active layer visibility → checkbox.
            if (g_layerVisCheckbox != IREntity::kNullEntity)
                IRPrefab::Widget::setCheckboxState(
                    g_layerVisCheckbox,
                    g_layerManager.isVisible(activeId)
                );
        }
    );

    // Shape bake system (T-286): runs in INPUT after WIDGET_APPLY_LIST so the
    // list selection is committed, and after WIDGET_APPLY_SLIDER so slider
    // values are committed. Reads the BAKE button and fires applyFillSDF.
    auto bakeSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorBake",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            using namespace IRVoxelEditor;
            if (g_bakeButton == IREntity::kNullEntity)
                return;
            if (!IRPrefab::Widget::wasClicked(g_bakeButton))
                return;
            if (g_editor.editableVoxelSet_ == IREntity::kNullEntity)
                return;

            static constexpr IRMath::SDF::ShapeType kShapeTypes[] = {
                IRMath::SDF::ShapeType::BOX,
                IRMath::SDF::ShapeType::SPHERE,
                IRMath::SDF::ShapeType::CYLINDER,
                IRMath::SDF::ShapeType::TORUS,
                IRMath::SDF::ShapeType::CONE,
                IRMath::SDF::ShapeType::ELLIPSOID,
            };
            static constexpr int kNumShapes = 6;

            const int sel = (g_bakeShapeList != IREntity::kNullEntity)
                                ? IRPrefab::Widget::listSelectedIndex(g_bakeShapeList)
                                : 1;
            const int idx = (sel >= 0 && sel < kNumShapes) ? sel : 1;
            const IRMath::SDF::ShapeType shapeType = kShapeTypes[idx];

            const float p1 = (g_bakeParam1Slider != IREntity::kNullEntity)
                                 ? IRPrefab::Widget::sliderValue(g_bakeParam1Slider)
                                 : 5.0f;
            const float p2 = (g_bakeParam2Slider != IREntity::kNullEntity)
                                 ? IRPrefab::Widget::sliderValue(g_bakeParam2Slider)
                                 : 3.0f;

            // Build SDF params for evaluate(): semantics depend on shape type.
            // evaluate() computes halfSize = vec3(params)*0.5 for box-family shapes.
            vec4 sdfParams{};
            switch (shapeType) {
            case IRMath::SDF::ShapeType::SPHERE:
                sdfParams = vec4(p1, 0.0f, 0.0f, 0.0f);
                break;
            case IRMath::SDF::ShapeType::TORUS:
                sdfParams = vec4(p1, p2, 0.0f, 0.0f);
                break;
            case IRMath::SDF::ShapeType::BOX:
            case IRMath::SDF::ShapeType::ELLIPSOID:
                sdfParams = vec4(p1 * 2.0f, p1 * 2.0f, p2 * 2.0f, 0.0f);
                break;
            case IRMath::SDF::ShapeType::CYLINDER:
            case IRMath::SDF::ShapeType::CONE:
            default:
                sdfParams = vec4(p1, p1, p2 * 2.0f, 0.0f);
                break;
            }

            const Color placeColor = kPaletteColors[g_editor.activeSwatchIdx_];
            auto &set = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
            applyFillSDF(g_editor.editableVoxelSet_, set, shapeType, sdfParams, true, placeColor);
            commitStroke();
            IR_LOG_INFO("Bake: shape {} P1={:.1f} P2={:.1f}", idx, p1, p2);
        }
    );

    // Joint-authoring bind-pose sync (#1604, placement-vs-posing split in
    // #1610). A TRANSLATE_ARROW drag on a rig joint is authoring: recapture
    // the bind pose once at gesture end so bindPose_ tracks the authored
    // rest. A ROTATE_RING drag is FK posing and must NOT recapture — the
    // pose deforms the skinned voxels away from the bind by design (T
    // captures explicitly). Release is edge-detected against GIZMO_DRAG's
    // own state (handle → kNullEntity) so a plain scene click — or a drag
    // of a non-rig showcase gizmo — never touches the rig.
    const IRSystem::SystemId gizmoDragId = IRSystem::createSystem<IRSystem::GIZMO_DRAG>();
    auto jointBindSyncSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorJointBindSync",
        [](const C_GuiElement &) {},
        []() {},
        [gizmoDragId]() {
            const auto *drag =
                IRSystem::getSystemParams<IRSystem::System<IRSystem::GIZMO_DRAG>>(gizmoDragId);
            const bool releasedThisFrame =
                IRVoxelEditor::g_jointTool.lastDragHandle_ != IREntity::kNullEntity &&
                drag->dragHandle_ == IREntity::kNullEntity;
            IRVoxelEditor::g_jointTool.lastDragHandle_ = drag->dragHandle_;
            if (!releasedThisFrame)
                return;
            if (drag->dragKind_ != IRComponents::GizmoKind::TRANSLATE_ARROW)
                return; // rotate rings are FK posing (#1610), not bind authoring
            if (!IRVoxelEditor::isRigJoint(drag->dragAnchor_))
                return; // showcase / non-rig gizmos don't touch the rig
            IRVoxelEditor::recomputeJointBindPose();
        }
    );

    // Skeleton tree panel sync (#1607). Runs after WIDGET_APPLY_LIST so
    // list.selectedIndex_ already reflects any click from this frame.
    // Rebuilds list items from C_Skeleton.joints_ (names from C_JointName or
    // "bone_N" default), mirrors activeJointIdx_ <-> list selection, handles
    // rename button (writes C_JointName), and reparent button (rewrites
    // CHILD_OF + updates parentIdx_ + refreshes bindPose_).
    auto jointTreeSyncSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorJointTreeSync",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            using namespace IRVoxelEditor;
            if (g_skeletonList == IREntity::kNullEntity)
                return;

            auto &list = IREntity::getComponent<C_WidgetList>(g_skeletonList);

            if (g_jointTool.rigRoot_ != IREntity::kNullEntity) {
                const auto &skeleton = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_);
                const int count = static_cast<int>(skeleton.joints_.size());
                if (static_cast<int>(list.items_.size()) != count)
                    list.items_.resize(static_cast<std::size_t>(count));
                for (int i = 0; i < count; ++i) {
                    const IREntity::EntityId joint = skeleton.joints_[static_cast<std::size_t>(i)];
                    auto nameOpt = IREntity::getComponentOptional<C_JointName>(joint);
                    // Label is rebuilt each frame; a mutation counter on C_Skeleton
                    // could skip this for unchanged rigs (deferred — benign at ≤10 joints).
                    std::string label = (nameOpt.has_value() && !(*nameOpt)->name_.empty())
                                            ? std::to_string(i) + " " + (*nameOpt)->name_
                                            : "bone_" + std::to_string(i);
                    if (list.items_[static_cast<std::size_t>(i)] != label)
                        list.items_[static_cast<std::size_t>(i)] = std::move(label);
                }

                // Mirror activeJointIdx_ → list selection.
                if (list.selectedIndex_ != g_jointTool.activeJointIdx_)
                    IRPrefab::Widget::setListSelectedIndex(
                        g_skeletonList,
                        g_jointTool.activeJointIdx_
                    );
            } else {
                list.items_.clear();
                list.selectedIndex_ = -1;
            }

            // List click → update activeJointIdx_ and pre-fill rename input.
            if (IRPrefab::Widget::wasClicked(g_skeletonList)) {
                g_jointTool.activeJointIdx_ = list.selectedIndex_;
                if (g_jointRenameInput != IREntity::kNullEntity &&
                    g_jointTool.rigRoot_ != IREntity::kNullEntity && list.selectedIndex_ >= 0) {
                    const auto &sk = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_);
                    const int idx = list.selectedIndex_;
                    if (idx < static_cast<int>(sk.joints_.size())) {
                        auto nameOpt = IREntity::getComponentOptional<C_JointName>(
                            sk.joints_[static_cast<std::size_t>(idx)]
                        );
                        IRPrefab::Widget::setTextInputValue(
                            g_jointRenameInput,
                            (nameOpt.has_value() && !(*nameOpt)->name_.empty()) ? (*nameOpt)->name_
                                                                                : ""
                        );
                    }
                }
            }

            // Rename button → write C_JointName on the active joint.
            if (g_jointRenameBtn != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_jointRenameBtn) &&
                g_jointTool.rigRoot_ != IREntity::kNullEntity && g_jointTool.activeJointIdx_ >= 0) {
                const auto &sk = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_);
                const int idx = g_jointTool.activeJointIdx_;
                if (idx < static_cast<int>(sk.joints_.size())) {
                    const std::string &newName =
                        IRPrefab::Widget::textInputValue(g_jointRenameInput);
                    IREntity::setComponent(
                        sk.joints_[static_cast<std::size_t>(idx)],
                        C_JointName{newName}
                    );
                }
            }

            // Reparent button → rewrite CHILD_OF for the active joint.
            if (g_jointReparentBtn != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_jointReparentBtn) &&
                g_jointTool.rigRoot_ != IREntity::kNullEntity && g_jointTool.activeJointIdx_ >= 0) {
                const auto &sk = IREntity::getComponent<C_Skeleton>(g_jointTool.rigRoot_);
                const int idx = g_jointTool.activeJointIdx_;
                const int count = static_cast<int>(sk.joints_.size());
                if (idx < count) {
                    int newParentIdx = -1;
                    try {
                        newParentIdx =
                            std::stoi(IRPrefab::Widget::textInputValue(g_jointReparentInput));
                    } catch (...) {
                        newParentIdx = -1;
                    }
                    const IREntity::EntityId joint = sk.joints_[static_cast<std::size_t>(idx)];
                    // Guard: can't parent to self or out-of-range bone.
                    // No cycle detection — A→B→A passes this guard and produces a stale
                    // bindPose_ from recomputeJointBindPose (bounded loop, no crash).
                    if (newParentIdx != idx && (newParentIdx < 0 || newParentIdx < count)) {
                        const IREntity::EntityId newParentEntity =
                            (newParentIdx < 0) ? g_jointTool.rigRoot_
                                               : sk.joints_[static_cast<std::size_t>(newParentIdx)];
                        IREntity::setParent(joint, newParentEntity);
                        g_jointTool.parentIdx_[static_cast<std::size_t>(idx)] = newParentIdx;
                        recomputeJointBindPose();
                    }
                }
            }
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
         IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_LIST>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
         scrubberSystem,
         layerSyncSystem,
         loftInputSystem,
         bakeSystem,
         paletteUpdateSystem,
         bonePaintUpdateSystem,
         placeEraseSystem,
         IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create(),
         IRSystem::createSystem<IRSystem::GIZMO_HOVER>(),
         gizmoDragId,
         jointBindSyncSystem,
         jointTreeSyncSystem}
    );

    // GPU voxel-position prepass (#1396) + joint skin-matrix upload (#1603) +
    // per-voxel bone→slot seeding (#1605) — the FK live-deform substrate
    // (#1610). UPDATE_JOINT_MATRICES must run AFTER PROPAGATE_TRANSFORM
    // (UPDATE pipeline, earlier this frame) and BEFORE
    // UPDATE_VOXEL_POSITIONS_GPU so binding 18 holds the skin matrices when
    // the prepass dispatches. Both are no-ops until a voxel set opts in via
    // gpuTransformSlot_ (the seeded rig does), so an unrigged scene renders
    // byte-identically. Created up-front so their SystemIds can order the
    // render pipeline below.
    const IRSystem::SystemId updateVoxelPositionsId =
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>();
    const IRSystem::SystemId updateJointMatricesId =
        IRSystem::createSystem<IRSystem::UPDATE_JOINT_MATRICES>();

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.push_front(animPlaybackSystem);
    renderPipeline.push_front(rotateSystem);
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            updateJointMatricesId,
            updateVoxelPositionsId,
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            // TEXT_TO_TRIXEL clears the GUI canvas to transparent in its
            // beginTick (`canvasTextures_->clear()`). Without this stage,
            // the GUI canvas keeps stale pixels — when composited over the
            // main canvas by TRIXEL_TO_FRAMEBUFFER, the result is an
            // opaque-black overlay that hides the 3D scene. Widget renders
            // must come AFTER the clear so their pixels survive.
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            loftRenderSystem,
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LIST>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_TEXT_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_COLOR_SWATCH>(),
            helpRenderSystem,
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );

    if (IREngine::args().autoScreenshotWarmupFrames() > 0) {
        IRVideo::GuiTestConfig cfg{};
        cfg.warmupFrames_ = IREngine::args().autoScreenshotWarmupFrames();
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRVoxelEditor::kGuiTestShots;
        cfg.numShots_ =
            sizeof(IRVoxelEditor::kGuiTestShots) / sizeof(IRVoxelEditor::kGuiTestShots[0]);
        // P3 (#1796): evaluate GUI assertions at each shot's capture frame. The
        // assertion tables themselves are populated later in initEntities (once
        // the widget entities exist), before the game loop fires this callback.
        cfg.onAssertFrame_ = &IRVoxelEditor::onGuiAssertFrame;
        renderPipeline.push_back(IRVideo::createGuiTestSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    // Register camera commands individually so we can own the Escape binding.
    // (registerCameraCommands() also binds Escape→CLOSE_WINDOW, which conflicts
    // with the drag-cancel handler below — we handle Escape ourselves.)
    IRCommand::createCommand<IRCommand::ZOOM_IN>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonEqual
    );
    IRCommand::createCommand<IRCommand::ZOOM_OUT>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonMinus
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_START>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonW
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_START>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonS
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_START>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonA
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_START>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonD
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_END>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::RELEASED,
        IRInput::KeyMouseButtons::kKeyButtonW
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_END>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::RELEASED,
        IRInput::KeyMouseButtons::kKeyButtonS
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_END>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::RELEASED,
        IRInput::KeyMouseButtons::kKeyButtonA
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_END>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::RELEASED,
        IRInput::KeyMouseButtons::kKeyButtonD
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonQ,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q - 1) * IRMath::kHalfPi);
        }
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonE,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q + 1) * IRMath::kHalfPi);
        }
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonSpace,
        []() {
            IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
            IRPrefab::Camera::setYaw(0.0f);
        }
    );

    // X/Y/Z: toggle mirror-symmetry axis.
    auto logSymmetry = []() {
        IR_LOG_INFO(
            "Symmetry: X=%s Y=%s Z=%s",
            IRVoxelEditor::g_symmetry.enableX_ ? "ON" : "OFF",
            IRVoxelEditor::g_symmetry.enableY_ ? "ON" : "OFF",
            IRVoxelEditor::g_symmetry.enableZ_ ? "ON" : "OFF"
        );
    };
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonX,
        [logSymmetry]() {
            IRVoxelEditor::g_symmetry.enableX_ = !IRVoxelEditor::g_symmetry.enableX_;
            logSymmetry();
        }
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonY,
        [logSymmetry]() {
            IRVoxelEditor::g_symmetry.enableY_ = !IRVoxelEditor::g_symmetry.enableY_;
            logSymmetry();
        }
    );
    // Ctrl+Z — undo. Bare Z — toggle Z-mirror. Both share the same key;
    // modifier checks disambiguate inline since IRCommand bindings don't
    // take modifier masks.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonZ,
        []() {
            if (IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u)) {
                IRVoxelEditor::undoOne();
            }
        }
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonZ,
        [logSymmetry]() {
            if (!IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u)) {
                IRVoxelEditor::g_symmetry.enableZ_ = !IRVoxelEditor::g_symmetry.enableZ_;
                logSymmetry();
            }
        }
    );

    // V — toggle erase-fill mode (#766 Part 2b): the left-click place / box /
    // line / face gestures ERASE instead of place while it is on. Right-click
    // single-voxel erase is unaffected.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonV,
        []() {
            IRVoxelEditor::g_eraseMode = !IRVoxelEditor::g_eraseMode;
            IR_LOG_INFO("Erase-fill mode: {}", IRVoxelEditor::g_eraseMode ? "ON" : "OFF");
        }
    );

    // Frame-based animation controls (T-214, F-1.4). Keys not taken by
    // T-211 (Q/E/Space/Z), T-212 (X/Y/Z symmetry), or T-213 (K/[/]/H —
    // layer system): Left/Right for frame nav, P play/pause, A add
    // blank frame, D duplicate, Backspace delete, L loop-mode toggle.

    // Left arrow — go to previous frame.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonLeft,
        []() { IRVoxelEditor::switchToFrame(IRVoxelEditor::g_anim.activeFrame_ - 1); }
    );

    // Right arrow — go to next frame.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonRight,
        []() { IRVoxelEditor::switchToFrame(IRVoxelEditor::g_anim.activeFrame_ + 1); }
    );

    // P — toggle play / pause; reset the elapsed timer and forward
    // direction so resuming always starts cleanly.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonP,
        []() {
            auto &anim = IRVoxelEditor::g_anim;
            anim.playing_ = !anim.playing_;
            anim.elapsed_ = 0.0f;
            if (anim.playing_)
                anim.playDirection_ = 1;
            IR_LOG_INFO(
                "Playback: %s  (%d frames at %.0f FPS)",
                anim.playing_ ? "PLAYING" : "PAUSED",
                anim.frameCount(),
                anim.fps_
            );
        }
    );

    // A — add a blank frame after the current frame and switch to it.
    // Snapshot the live voxels into the active frame first so the user
    // doesn't lose their pose when stepping forward to a new blank.
    // The new frame's voxels_ is empty, so loadFrameToLive falls into
    // its size-mismatch branch and fills the live target with blank.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonA,
        []() {
            auto &anim = IRVoxelEditor::g_anim;
            auto &editor = IRVoxelEditor::g_editor;
            IRVoxelEditor::snapshotLiveToFrame(anim.activeFrame_);
            // Save departing frame's undo state; new frame starts clean.
            const int oldFrame = anim.activeFrame_;
            if (oldFrame < static_cast<int>(editor.perFrameUndoStacks_.size())) {
                editor.perFrameUndoStacks_[oldFrame] = std::move(editor.undoRecords_);
                editor.perFrameUndoBytes_[oldFrame] = editor.undoTotalBytes_;
            }
            editor.undoRecords_ = {};
            editor.undoTotalBytes_ = 0;
            anim.addBlankFrame();
            // Keep perFrameUndoStacks_ in sync: insert empty slot for new frame.
            editor.perFrameUndoStacks_.insert(
                editor.perFrameUndoStacks_.begin() + anim.activeFrame_,
                {}
            );
            editor.perFrameUndoBytes_.insert(
                editor.perFrameUndoBytes_.begin() + anim.activeFrame_,
                std::size_t{0}
            );
            IRVoxelEditor::loadFrameToLive(anim.activeFrame_);
            IR_LOG_INFO("Added blank frame %d / %d", anim.activeFrame_ + 1, anim.frameCount());
        }
    );

    // D — duplicate the current frame. Snapshot the live voxels into
    // the active frame first so the copy is current. The duplicate
    // starts with an empty undo history — its voxels are the same as
    // the source frame, so no undo-restoration is needed to get back
    // to the initial state; clearing is the simpler and safer choice.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonD,
        []() {
            auto &anim = IRVoxelEditor::g_anim;
            auto &editor = IRVoxelEditor::g_editor;
            IRVoxelEditor::snapshotLiveToFrame(anim.activeFrame_);
            // Save source frame's undo state; the duplicate starts clean.
            const int oldFrame = anim.activeFrame_;
            if (oldFrame < static_cast<int>(editor.perFrameUndoStacks_.size())) {
                editor.perFrameUndoStacks_[oldFrame] = std::move(editor.undoRecords_);
                editor.perFrameUndoBytes_[oldFrame] = editor.undoTotalBytes_;
            }
            editor.undoRecords_ = {};
            editor.undoTotalBytes_ = 0;
            anim.duplicateCurrentFrame();
            // Insert empty undo slot for the duplicate frame.
            editor.perFrameUndoStacks_.insert(
                editor.perFrameUndoStacks_.begin() + anim.activeFrame_,
                {}
            );
            editor.perFrameUndoBytes_.insert(
                editor.perFrameUndoBytes_.begin() + anim.activeFrame_,
                std::size_t{0}
            );
            IR_LOG_INFO("Duplicated frame %d / %d", anim.activeFrame_ + 1, anim.frameCount());
        }
    );

    // Backspace — delete the current frame (minimum 1 frame). The
    // deleted frame's undo history is discarded; the surviving active
    // frame's saved undo state is promoted into the hot slot.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonBackspace,
        []() {
            auto &anim = IRVoxelEditor::g_anim;
            auto &editor = IRVoxelEditor::g_editor;
            if (anim.frameCount() <= 1) {
                IR_LOG_INFO("Cannot delete the only frame.");
                return;
            }
            const int deletedFrame = anim.activeFrame_;
            // Discard the deleted frame's hot undo state.
            editor.undoRecords_ = {};
            editor.undoTotalBytes_ = 0;
            // Remove the cold slot for the frame being deleted.
            if (deletedFrame < static_cast<int>(editor.perFrameUndoStacks_.size())) {
                editor.perFrameUndoStacks_.erase(editor.perFrameUndoStacks_.begin() + deletedFrame);
                editor.perFrameUndoBytes_.erase(editor.perFrameUndoBytes_.begin() + deletedFrame);
            }
            anim.deleteCurrentFrame();
            // Promote the new active frame's cold undo state to the hot slot.
            const int newFrame = anim.activeFrame_;
            if (newFrame < static_cast<int>(editor.perFrameUndoStacks_.size())) {
                editor.undoRecords_ = std::move(editor.perFrameUndoStacks_[newFrame]);
                editor.undoTotalBytes_ = editor.perFrameUndoBytes_[newFrame];
                editor.perFrameUndoStacks_[newFrame] = {};
                editor.perFrameUndoBytes_[newFrame] = 0;
            }
            IRVoxelEditor::loadFrameToLive(anim.activeFrame_);
            IR_LOG_INFO("Deleted frame (now %d / %d)", anim.activeFrame_ + 1, anim.frameCount());
        }
    );

    // L — toggle loop mode between LOOP and PING-PONG.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonL,
        []() {
            auto &anim = IRVoxelEditor::g_anim;
            anim.loopMode_ = (anim.loopMode_ == IRVoxelEditor::LoopMode::LOOP)
                                 ? IRVoxelEditor::LoopMode::PING_PONG
                                 : IRVoxelEditor::LoopMode::LOOP;
            IR_LOG_INFO(
                "Loop mode: %s",
                anim.loopMode_ == IRVoxelEditor::LoopMode::LOOP ? "LOOP" : "PING-PONG"
            );
        }
    );

    // Escape: cancel drag if active, otherwise close the window.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonEscape,
        []() {
            if (IRVoxelEditor::g_fillTool.dragging_) {
                IRVoxelEditor::g_fillTool.dragging_ = false;
                if (IRVoxelEditor::g_fillTool.ghostEntity_ != IREntity::kNullEntity) {
                    IREntity::getComponent<C_ShapeDescriptor>(
                        IRVoxelEditor::g_fillTool.ghostEntity_
                    )
                        .flags_ = IRMath::SDF::SHAPE_FLAG_NONE;
                }
                IR_LOG_INFO("Fill drag cancelled (Escape).");
                return;
            }
            IRWindow::closeWindow();
        }
    );

    // F — toggle loft mode on/off. Cancels any active fill drag and hides
    // the ghost shape so it doesn't linger over the mask panels.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonF,
        []() {
            auto &loft = IRVoxelEditor::g_loftTool;
            loft.active_ = !loft.active_;
            IRVoxelEditor::g_fillTool.dragging_ = false;
            if (IRVoxelEditor::g_fillTool.ghostEntity_ != IREntity::kNullEntity) {
                IREntity::getComponent<C_ShapeDescriptor>(IRVoxelEditor::g_fillTool.ghostEntity_)
                    .flags_ = IRMath::SDF::SHAPE_FLAG_NONE;
            }
            IR_LOG_INFO("Loft mode: {}", loft.active_ ? "ON" : "OFF");
        }
    );

    // Enter — stamp the current loft masks into the scene using the active
    // palette color. Does nothing if loft mode is inactive or masks are empty.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonEnter,
        []() {
            if (!IRVoxelEditor::g_loftTool.active_)
                return;
            const Color placeColor =
                IRVoxelEditor::kPaletteColors[IRVoxelEditor::g_editor.activeSwatchIdx_];
            IRVoxelEditor::applyLoft(placeColor);
            IR_LOG_INFO("Loft stamped.");
        }
    );

    // C — clear both loft masks when in loft mode. No-op outside loft mode
    // so the key stays available for future bindings.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonC,
        []() {
            if (!IRVoxelEditor::g_loftTool.active_)
                return;
            auto &loft = IRVoxelEditor::g_loftTool;
            std::fill(loft.maskXZ_.begin(), loft.maskXZ_.end(), false);
            std::fill(loft.maskYZ_.begin(), loft.maskYZ_.end(), false);
            IR_LOG_INFO("Loft masks cleared.");
        }
    );

    // K: add a new layer (auto-named from count, immediately becomes active).
    // N is reserved for frame-animation's "add blank frame" binding.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonK,
        []() {
            std::uint8_t id = IRVoxelEditor::g_layerManager.addLayer(
                "layer " + std::to_string(IRVoxelEditor::g_layerManager.layers().size())
            );
            if (id != 0)
                IRVoxelEditor::g_layerManager.setActiveLayer(id);
            IR_LOG_INFO("Layers after add:");
            IRVoxelEditor::logLayerState();
        }
    );

    // [: select previous layer in display order (wraps around)
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonLeftBracket,
        []() {
            IRVoxelEditor::g_layerManager.selectPrevLayer();
            IRVoxelEditor::logLayerState();
        }
    );

    // ]: select next layer in display order (wraps around)
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonRightBracket,
        []() {
            IRVoxelEditor::g_layerManager.selectNextLayer();
            IRVoxelEditor::logLayerState();
        }
    );

    // J — toggle skeletal joint-authoring mode (#1604). While on, B adds a
    // joint and R starts a fresh chain off the rig root.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonJ,
        []() {
            IRVoxelEditor::g_jointTool.active_ = !IRVoxelEditor::g_jointTool.active_;
            IR_LOG_INFO("Joint authoring: {}", IRVoxelEditor::g_jointTool.active_ ? "ON" : "OFF");
        }
    );

    // B — add a joint, chained to the active joint (or the rig root). No-op
    // outside joint mode so the key stays free for other tools.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonB,
        []() {
            if (!IRVoxelEditor::g_jointTool.active_)
                return;
            IRVoxelEditor::addJointAuthored();
        }
    );

    // R — start a new bone chain: the next B parents to the rig root rather
    // than the last-added joint. No-op outside joint mode.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonR,
        []() {
            if (!IRVoxelEditor::g_jointTool.active_)
                return;
            IRVoxelEditor::resetJointChain();
        }
    );

    // N — toggle bone-paint mode (#1608). While on, left-click writes
    // bone_id_ to the hit voxel and tints it with the selected bone's
    // display color. The bone selector swatch panel drives activeBoneIdx_.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonN,
        []() {
            IRVoxelEditor::g_bonePaint.active_ = !IRVoxelEditor::g_bonePaint.active_;
            IR_LOG_INFO(
                "Bone paint mode: {}  (active bone: {})",
                IRVoxelEditor::g_bonePaint.active_ ? "ON" : "OFF",
                IRVoxelEditor::g_bonePaint.activeBoneIdx_
            );
        }
    );

    // T — set current pose as bind (#1610): the posed joint chain becomes
    // the new rest, skin matrices return to identity, and the rig's voxels
    // relax in place. No-op outside joint mode.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonT,
        []() {
            if (!IRVoxelEditor::g_jointTool.active_)
                return;
            IRVoxelEditor::setCurrentPoseAsBind();
        }
    );

    // H: toggle active layer visibility. Iterates C_VoxelSetNew and updates
    // voxel alpha so hidden layers vanish from the viewport immediately.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonH,
        []() {
            const std::uint8_t layerId = IRVoxelEditor::g_layerManager.activeLayerId();
            bool nowVisible = IRVoxelEditor::g_layerManager.toggleLayerVisibility(layerId);
            IRVoxelEditor::applyLayerVisibility(layerId, nowVisible);
            IR_LOG_INFO("Layer {} visibility -> {}", layerId, nowVisible ? "shown" : "hidden");
        }
    );

    // Ctrl+S — save scene (all frames + layer metadata) to disk.
    // Snapshots the live voxels into the active frame before writing.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonS,
        []() {
            if (!IRInput::checkKeyMouseModifiers(
                    IRInput::kModifierControl,
                    IRInput::kModifierShift
                ))
                return;
            auto &anim = IRVoxelEditor::g_anim;
            IRVoxelEditor::snapshotLiveToFrame(anim.activeFrame_);
            std::vector<std::vector<IRComponents::C_Voxel>> snapshots;
            snapshots.reserve(static_cast<std::size_t>(anim.frameCount()));
            for (const auto &f : anim.frames_)
                snapshots.push_back(f.voxels_);
            auto res = IRVoxelEditor::saveEditorScene(
                std::string(IRVoxelEditor::kSceneSaveDir),
                std::string(IRVoxelEditor::kSceneBaseName),
                snapshots,
                IRVoxelEditor::g_editableSceneSize,
                IRVoxelEditor::g_layerManager,
                anim,
                IRVoxelEditor::g_symmetry
            );
            if (res.ok_)
                IR_LOG_INFO(
                    "Scene saved to {}/{}",
                    IRVoxelEditor::kSceneSaveDir,
                    IRVoxelEditor::kSceneBaseName
                );
            else
                IR_LOG_ERROR("Save failed: {}", res.errorMsg_);
        }
    );

    // Ctrl+Shift+S — save skeleton to {kSceneSaveDir}/{kSceneBaseName}.rig.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonS,
        []() {
            if (!IRInput::checkKeyMouseModifiers(
                    IRInput::kModifierControl | IRInput::kModifierShift,
                    0u
                ))
                return;
            if (IRVoxelEditor::g_jointTool.rigRoot_ == IREntity::kNullEntity) {
                IR_LOG_WARN("No rig to save — author joints with J + B first.");
                return;
            }
            auto res = IRVoxelEditor::saveRigScene(
                std::string(IRVoxelEditor::kSceneSaveDir),
                std::string(IRVoxelEditor::kSceneBaseName),
                IRVoxelEditor::g_jointTool.rigRoot_,
                IRVoxelEditor::g_jointTool.parentIdx_
            );
            if (res.ok_)
                IR_LOG_INFO(
                    "Rig saved to {}/{}",
                    IRVoxelEditor::kSceneSaveDir,
                    IRVoxelEditor::kSceneBaseName
                );
            else
                IR_LOG_ERROR("Rig save failed: {}", res.errorMsg_);
        }
    );

    // Ctrl+O — load scene from disk, replacing all frames and layer state.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonO,
        []() {
            if (!IRInput::checkKeyMouseModifiers(
                    IRInput::kModifierControl,
                    IRInput::kModifierShift
                ))
                return;
            auto loaded = IRVoxelEditor::loadEditorScene(
                std::string(IRVoxelEditor::kSceneSaveDir),
                std::string(IRVoxelEditor::kSceneBaseName)
            );
            if (!loaded.ok_) {
                IR_LOG_ERROR("Load failed: {}", loaded.errorMsg_);
                return;
            }
            auto &anim = IRVoxelEditor::g_anim;
            auto &editor = IRVoxelEditor::g_editor;

            // Replace animation frames.
            anim.frames_.clear();
            for (auto &snap : loaded.frameSnapshots_)
                anim.frames_.push_back(IRVoxelEditor::VoxelFrame{std::move(snap)});
            if (anim.frames_.empty())
                anim.frames_.emplace_back();
            anim.fps_ = loaded.fps_;
            anim.loopMode_ = loaded.loopMode_;
            anim.playing_ = false;
            anim.elapsed_ = 0.0f;
            anim.activeFrame_ = IRMath::clamp(loaded.activeFrame_, 0, anim.frameCount() - 1);

            IRVoxelEditor::g_symmetry = loaded.symmetry_;

            if (!loaded.layers_.empty())
                IRVoxelEditor::g_layerManager
                    .resetAndLoad(loaded.layers_, loaded.activeLayerId_, loaded.nextLayerId_);

            // Reset per-frame undo state to match the new frame count.
            editor.undoRecords_ = {};
            editor.undoTotalBytes_ = 0;
            editor.perFrameUndoStacks_.assign(static_cast<std::size_t>(anim.frameCount()), {});
            editor.perFrameUndoBytes_.assign(static_cast<std::size_t>(anim.frameCount()), 0);

            IRVoxelEditor::loadFrameToLive(anim.activeFrame_);

            // Re-apply visibility for any hidden layers.
            for (const auto &layer : IRVoxelEditor::g_layerManager.layers()) {
                if (!layer.visible_)
                    IRVoxelEditor::applyLayerVisibility(layer.id_, false);
            }

            IR_LOG_INFO(
                "Scene loaded from {}/{} ({} frames)",
                IRVoxelEditor::kSceneSaveDir,
                IRVoxelEditor::kSceneBaseName,
                anim.frameCount()
            );
        }
    );

    // Ctrl+Shift+O — load skeleton from {kSceneSaveDir}/{kSceneBaseName}.rig.
    // Destroys existing joint entities and their gizmo children, then
    // reconstructs the skeleton from the saved .rig file.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonO,
        []() {
            if (!IRInput::checkKeyMouseModifiers(
                    IRInput::kModifierControl | IRInput::kModifierShift,
                    0u
                ))
                return;

            auto loaded = IRVoxelEditor::loadRigScene(
                std::string(IRVoxelEditor::kSceneSaveDir),
                std::string(IRVoxelEditor::kSceneBaseName)
            );
            if (!loaded.ok_) {
                IR_LOG_ERROR("Rig load failed: {}", loaded.errorMsg_);
                return;
            }

            // Collect existing joint ids.
            std::vector<IREntity::EntityId> oldJointIds;
            IREntity::forEachComponent<IRComponents::C_Joint>(
                [&](IREntity::EntityId id, IRComponents::C_Joint &) { oldJointIds.push_back(id); }
            );

            // Collect gizmo handles anchored to those joints, then destroy
            // gizmos first so no child tries to read a destroyed parent.
            {
                std::vector<IREntity::EntityId> oldGizmoIds;
                IREntity::forEachComponent<IRComponents::C_GizmoHandle>(
                    [&](IREntity::EntityId id, IRComponents::C_GizmoHandle &h) {
                        for (const auto jid : oldJointIds) {
                            if (h.anchorEntity_ == jid) {
                                oldGizmoIds.push_back(id);
                                break;
                            }
                        }
                    }
                );
                for (const auto id : oldGizmoIds)
                    IREntity::destroyEntity(id);
            }
            for (const auto id : oldJointIds)
                IREntity::destroyEntity(id);

            // Reset skeleton and authoring tool state.
            const IREntity::EntityId rigRoot = IRVoxelEditor::ensureRigRoot();
            {
                auto &skeleton = IREntity::getComponent<IRComponents::C_Skeleton>(rigRoot);
                skeleton.joints_.clear();
                skeleton.bindPose_.clear();
            }
            IRVoxelEditor::g_jointTool.parentIdx_.clear();
            IRVoxelEditor::g_jointTool.activeJointIdx_ = -1;
            IRVoxelEditor::g_jointTool.bindPoseRecaptured_ = false;

            // Reconstruct joint entities from the loaded rig.
            const auto &rig = loaded.rig_;
            const std::size_t count = rig.joints_.size();
            std::vector<IREntity::EntityId> newJoints;
            newJoints.reserve(count);

            for (std::size_t i = 0; i < count; ++i) {
                const auto &j = rig.joints_[i];
                const IRMath::vec3 t{j.translation_.x, j.translation_.y, j.translation_.z};
                const IREntity::EntityId joint = IREntity::createEntity(
                    IRComponents::C_LocalTransform{t, j.rotation_},
                    IRComponents::C_Joint{}
                );
                const bool atRigRoot = j.parentIndex_ == static_cast<std::uint32_t>(i) ||
                                       j.parentIndex_ >= static_cast<std::uint32_t>(count);
                IR_ASSERT(
                    atRigRoot || j.parentIndex_ < i,
                    ".rig parentIndex forward-reference — not supported by linear reconstruction"
                );
                const IREntity::EntityId parentEntity =
                    atRigRoot ? rigRoot : newJoints[j.parentIndex_];
                IREntity::setParent(joint, parentEntity);
                IRPrefab::Gizmo::createJointMarker(joint);
                IRPrefab::Gizmo::createTranslateGizmoForAnchor(joint);
                newJoints.push_back(joint);
                IRVoxelEditor::g_jointTool.parentIdx_.push_back(
                    atRigRoot ? -1 : static_cast<int>(j.parentIndex_)
                );
            }

            // Re-fetch skeleton after all createEntity calls to avoid
            // stale references, then populate joints and bind pose.
            {
                auto &skeleton = IREntity::getComponent<IRComponents::C_Skeleton>(rigRoot);
                skeleton.joints_.insert(skeleton.joints_.end(), newJoints.begin(), newJoints.end());
                skeleton.bindPose_ = IRPrefab::Rig::bindPose(rig);
            }

            IR_LOG_INFO(
                "Rig loaded: {} joints from {}/{}",
                count,
                IRVoxelEditor::kSceneSaveDir,
                IRVoxelEditor::kSceneBaseName
            );
        }
    );
}

void initEntities() {
    using IRVoxelEditor::g_editor;

    // Pre-size the in-flight stroke buffer so a click's append doesn't
    // pay a first-time allocation cost.
    g_editor.pendingStroke_.edits_.reserve(IRVoxelEditor::kUndoStrokeReserve);

    // Per-frame undo slots — one per animation frame, always kept in sync
    // with g_anim.frames_. Starts with one entry for the initial frame.
    g_editor.perFrameUndoStacks_.resize(IRVoxelEditor::g_anim.frameCount());
    g_editor.perFrameUndoBytes_.resize(IRVoxelEditor::g_anim.frameCount(), 0);

    constexpr float kFloorZ = 2.0f;

    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, kFloorZ)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(40.0f, 40.0f, 1.0f, 0.0f),
            Color{55, 60, 75, 255}
        }
    );

    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(16.0f, 0.5f, 0.5f, 0.0f),
            Color{180, 60, 60, 255}
        }
    );

    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(0.5f, 16.0f, 0.5f, 0.0f),
            Color{60, 180, 60, 255}
        }
    );

    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(1.5f, 1.5f, 1.5f, 0.0f),
            Color{220, 220, 240, 255}
        }
    );

    // F-0.5 Phase 1 gizmo primitives — kept around the perimeter as
    // visual references for the gizmo render pass.
    {
        IREntity::EntityId translateGizmo = IRPrefab::Gizmo::createTranslateGizmo();
        IREntity::getComponent<C_LocalTransform>(translateGizmo).translation_ =
            vec3(-12.0f, 12.0f, -3.0f);

        IREntity::EntityId rotateGizmo = IRPrefab::Gizmo::createRotateGizmo();
        IREntity::getComponent<C_LocalTransform>(rotateGizmo).translation_ =
            vec3(12.0f, 12.0f, -3.0f);

        IREntity::EntityId scaleGizmo = IRPrefab::Gizmo::createScaleGizmo();
        IREntity::getComponent<C_LocalTransform>(scaleGizmo).translation_ =
            vec3(-12.0f, -12.0f, -3.0f);

        IREntity::EntityId jointMarker = IRPrefab::Gizmo::createJointMarker();
        IREntity::getComponent<C_LocalTransform>(jointMarker).translation_ =
            vec3(8.0f, -12.0f, -3.0f);

        IREntity::EntityId bindPointMarker = IRPrefab::Gizmo::createBindPointMarker();
        IREntity::getComponent<C_LocalTransform>(bindPointMarker).translation_ =
            vec3(12.0f, -12.0f, -3.0f);

        IREntity::EntityId ikMarker = IRPrefab::Gizmo::createIKMarker();
        IREntity::getComponent<C_LocalTransform>(ikMarker).translation_ =
            vec3(16.0f, -12.0f, -3.0f);
    }

    // F-2.5 (#1604) joint-authoring starter rig — a short bone chain so the
    // feature is visible on launch and in auto-screenshots (same spirit as the
    // perimeter gizmo references above). Author more with J (toggle mode) + B
    // (add joint); R starts a new chain off the rig root.
    IRVoxelEditor::seedDemoSkeleton();

    // Editable voxel set — the place/erase target. Allocated empty
    // (default color, alpha=255 so cells are active at start) then
    // every voxel is deactivated so the user starts from an empty
    // scene. A single floor row stays activated as a "ground" for the
    // first click to land on. Architect D1: the size is a named
    // constant on the editor side, not hardcoded inline.
    g_editor.editableVoxelSet_ = IREntity::createEntity(
        C_LocalTransform{IRVoxelEditor::g_editableSceneOrigin},
        C_VoxelSetNew{IRVoxelEditor::g_editableSceneSize, Color{200, 200, 210, 255}}
    );
    IRVoxelEditor::g_sceneVoxelSetEntity = g_editor.editableVoxelSet_;
    {
        auto &set = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
        const int sx = set.size_.x;
        const int sy = set.size_.y;
        const int sz = set.size_.z;
        IRVoxelEditor::g_loftTool.maskXZ_.assign(static_cast<std::size_t>(sx * sz), false);
        IRVoxelEditor::g_loftTool.maskYZ_.assign(static_cast<std::size_t>(sy * sz), false);
        set.deactivateAll();
        // Ground plane at z == size_.z - 1: flat gray, gives the user
        // something to click before placing any voxels themselves.
        set.fillPlane(2, set.size_.z - 1, Color{120, 120, 130, 255});
    }

    // Smaller satellite voxel sets — exercise multi-`C_VoxelSetNew`
    // picking from T-219 and give the user secondary targets to click
    // on. Their colors stay fixed so it's obvious which click landed
    // on the editable set versus a satellite.
    IREntity::createEntity(
        C_LocalTransform{vec3(-16.0f, 0.0f, -6.0f)},
        C_VoxelSetNew{ivec3(4, 4, 4), Color{120, 180, 240, 255}}
    );
    IREntity::createEntity(
        C_LocalTransform{vec3(16.0f, 0.0f, -6.0f)},
        C_VoxelSetNew{ivec3(4, 4, 4), Color{240, 180, 120, 255}}
    );

    // Canvas setup (unchanged from the F-0.5 baseline).
    IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});

    // Render the GUI at native framebuffer resolution so panels/text are
    // small and crisp instead of the coarse iso-canvas default. The editor
    // lays its widgets out relative to the resulting GUI canvas size below.
    IRRender::setGuiCanvasFullResolution();

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));

    // Palette panel — fixed top-left dock at 200×220 trixels. The 16
    // swatches lay out in a 4×4 grid below the title. Architect D4:
    // mutable palette + voxels store raw RGBA, so editing a swatch
    // (future workflow) does not repaint already-placed voxels.
    // Palette docks to the bottom-left of the GUI canvas so it sits
    // below the iso scene render and never covers the edit target.
    // Sized to fit a 4×4 grid of 22-trixel swatches inside a
    // ~115×165-trixel panel — small enough not to crowd the workspace.
    constexpr ivec2 kPanelPos{4, 240};
    constexpr ivec2 kPanelSize{120, 175};
    constexpr int kSwatchSize = 20;
    constexpr int kSwatchGap = 4;
    constexpr int kSwatchOriginX = kPanelPos.x + 8;
    constexpr int kSwatchOriginY = kPanelPos.y + 48;
    constexpr int kGridCols = 4;

    g_editor.palettePanel_ = IRPrefab::Widget::makePanel(kPanelPos, kPanelSize, "PALETTE");
    // makePanel skips C_HitBox2DGui so it doesn't consume mouse hover. Add it
    // manually so clicks on the panel background (title bar, label gap, padding)
    // are blocked from falling through to the scene picker.
    IREntity::setComponent(g_editor.palettePanel_, IRComponents::C_HitBox2DGui{kPanelSize});
    // Left-align the hint to the panel padding so it doesn't overflow the
    // right edge ("CLICK A SWATCH" is ~111 trixels; the panel interior is ~116).
    IRPrefab::Widget::makeLabel(ivec2(kPanelPos.x + 4, kPanelPos.y + 36), "CLICK A SWATCH");

    g_editor.paletteSwatches_.reserve(IRVoxelEditor::kPaletteCount);
    for (int i = 0; i < IRVoxelEditor::kPaletteCount; ++i) {
        const int row = i / kGridCols;
        const int col = i % kGridCols;
        const ivec2 pos(
            kSwatchOriginX + col * (kSwatchSize + kSwatchGap),
            kSwatchOriginY + row * (kSwatchSize + kSwatchGap)
        );
        g_editor.paletteSwatches_.push_back(
            IRPrefab::Widget::makeColorSwatch(
                pos,
                ivec2(kSwatchSize, kSwatchSize),
                IRVoxelEditor::kPaletteColors[i],
                i == 0
            )
        );
    }

    // Animation controls panel (T-214, F-1.4) — sits below the palette
    // panel. Frame scrubber: drag to navigate frames smoothly. FPS slider:
    // drag to adjust playback speed (1–30 FPS). Both update in real time;
    // keyboard nav (Left/Right) and playback keep the scrubber thumb in sync.
    constexpr ivec2 kAnimPanelPos{4, 420};
    constexpr ivec2 kAnimPanelSize{120, 55};
    IRPrefab::Widget::makePanel(kAnimPanelPos, kAnimPanelSize, "ANIM");
    IRVoxelEditor::g_scrubberSlider = IRPrefab::Widget::makeSlider(
        ivec2(kAnimPanelPos.x + 4, kAnimPanelPos.y + 22),
        ivec2(112, 14),
        "FRAME",
        0.0f,
        static_cast<float>(IRVoxelEditor::g_anim.frameCount() - 1),
        0.0f
    );
    IRVoxelEditor::g_fpsSlider = IRPrefab::Widget::makeSlider(
        ivec2(kAnimPanelPos.x + 4, kAnimPanelPos.y + 38),
        ivec2(112, 14),
        "FPS",
        1.0f,
        30.0f,
        IRVoxelEditor::g_anim.fps_
    );

    // Layer panel (T-213, F-1.3) — second column alongside the PALETTE panel.
    // The list shows all layers with a "[H]" suffix on hidden ones. The
    // visibility checkbox and add/delete buttons are below the list. Keyboard
    // shortcuts K/[/]/H still work; the panel just makes the state visible.
    constexpr ivec2 kLayerPanelPos{130, 240};
    constexpr ivec2 kLayerPanelSize{120, 96};
    IRVoxelEditor::g_layerPanel =
        IRPrefab::Widget::makePanel(kLayerPanelPos, kLayerPanelSize, "LAYERS");
    IREntity::setComponent(
        IRVoxelEditor::g_layerPanel,
        IRComponents::C_HitBox2DGui{kLayerPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_layerPanel).zOrder_ = -1;

    IRVoxelEditor::g_layerList = IRPrefab::Widget::makeList(
        ivec2(kLayerPanelPos.x + 4, kLayerPanelPos.y + 18),
        ivec2(112, 52),
        {"default"},
        0,
        13
    );
    IRVoxelEditor::g_layerVisCheckbox = IRPrefab::Widget::makeCheckbox(
        ivec2(kLayerPanelPos.x + 4, kLayerPanelPos.y + 74),
        ivec2(60, 14),
        "visible",
        true
    );
    IRVoxelEditor::g_layerAddBtn = IRPrefab::Widget::makeButton(
        ivec2(kLayerPanelPos.x + 68, kLayerPanelPos.y + 74),
        ivec2(20, 14),
        "+"
    );
    IRVoxelEditor::g_layerDelBtn = IRPrefab::Widget::makeButton(
        ivec2(kLayerPanelPos.x + 92, kLayerPanelPos.y + 74),
        ivec2(20, 14),
        "-"
    );

    // Parametric shape bake panel (T-286). Sits below the LAYERS panel.
    // Shape list selects the SDF primitive; P1/P2 sliders set the primary and
    // secondary params; BAKE writes DENSE voxels into the active entity.
    // List itemHeight is one glyph row + 2-trixel gap so the 6 shape rows
    // don't touch (itemHeight == glyph height made adjacent rows overlap). The
    // sub-controls sit below the now-taller 6-row list (18 + 6*13 = 96).
    constexpr ivec2 kBakePanelPos{130, 342};
    constexpr ivec2 kBakePanelSize{120, 156};
    IRVoxelEditor::g_bakePanel = IRPrefab::Widget::makePanel(kBakePanelPos, kBakePanelSize, "BAKE");
    IREntity::setComponent(IRVoxelEditor::g_bakePanel, IRComponents::C_HitBox2DGui{kBakePanelSize});
    IRVoxelEditor::g_bakeShapeList = IRPrefab::Widget::makeList(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 18),
        ivec2(112, 78),
        {"BOX", "SPHERE", "CYLINDER", "TORUS", "CONE", "ELLIPSOID"},
        1,
        13
    );
    IRVoxelEditor::g_bakeParam1Slider = IRPrefab::Widget::makeSlider(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 100),
        ivec2(112, 14),
        "P1",
        0.5f,
        12.0f,
        8.0f
    );
    IRVoxelEditor::g_bakeParam2Slider = IRPrefab::Widget::makeSlider(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 118),
        ivec2(112, 14),
        "P2",
        0.5f,
        12.0f,
        3.0f
    );
    IRVoxelEditor::g_bakeButton = IRPrefab::Widget::makeButton(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 136),
        ivec2(112, 12),
        "BAKE"
    );

    // Bone selector panel (F-2.7 / #1608). kBoneSwatchCount swatches in a 2×4
    // grid; index 0 = identity (gray), indices 1..7 cycle through distinct hues.
    // Clicking a swatch sets g_bonePaint.activeBoneIdx_; N enables bone-paint mode.
    // Third column (x=256) atop the SKELETON panel — mirrors the LAYERS/BAKE
    // stack in column two so both bone panels stay on-screen.
    constexpr ivec2 kBonePanelPos{256, 240};
    constexpr ivec2 kBonePanelSize{120, 96};
    constexpr int kBoneSwatchSize = 20;
    constexpr int kBoneSwatchGap = 4;
    constexpr int kBoneSwatchOriginX = kBonePanelPos.x + 8;
    constexpr int kBoneSwatchOriginY = kBonePanelPos.y + 36;
    constexpr int kBoneGridCols = 4;

    IRVoxelEditor::g_bonePaint.bonePanel_ =
        IRPrefab::Widget::makePanel(kBonePanelPos, kBonePanelSize, "BONE");
    IREntity::setComponent(
        IRVoxelEditor::g_bonePaint.bonePanel_,
        IRComponents::C_HitBox2DGui{kBonePanelSize}
    );
    IRPrefab::Widget::makeLabel(ivec2(kBonePanelPos.x + 8, kBonePanelPos.y + 22), "N:ON/OFF");

    IRVoxelEditor::g_bonePaint.boneSwatches_.reserve(IRVoxelEditor::kBoneSwatchCount);
    for (int i = 0; i < IRVoxelEditor::kBoneSwatchCount; ++i) {
        const int row = i / kBoneGridCols;
        const int col = i % kBoneGridCols;
        const ivec2 pos(
            kBoneSwatchOriginX + col * (kBoneSwatchSize + kBoneSwatchGap),
            kBoneSwatchOriginY + row * (kBoneSwatchSize + kBoneSwatchGap)
        );
        IRVoxelEditor::g_bonePaint.boneSwatches_.push_back(
            IRPrefab::Widget::makeColorSwatch(
                pos,
                ivec2(kBoneSwatchSize, kBoneSwatchSize),
                IRVoxelEditor::kBoneColors[i],
                i == 0
            )
        );
    }

    // Skeleton tree panel (F-2.6, #1607). Sits below the BONE selector in the
    // third column (mirrors LAYERS→BAKE in column two), so the swatch grid and
    // the joint tree coexist without overlapping.
    // Shows the live joint list from C_Skeleton.joints_; clicking a row
    // selects that joint as the active bone (for B-chaining). The rename
    // row writes C_JointName; the reparent row rewrites the CHILD_OF
    // relation and updates parentIdx_ + bindPose_.
    constexpr ivec2 kSkeletonPanelPos{256, 342};
    constexpr ivec2 kSkeletonPanelSize{120, 114};
    IRVoxelEditor::g_skeletonPanel =
        IRPrefab::Widget::makePanel(kSkeletonPanelPos, kSkeletonPanelSize, "SKELETON");
    IREntity::setComponent(
        IRVoxelEditor::g_skeletonPanel,
        IRComponents::C_HitBox2DGui{kSkeletonPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_skeletonPanel).zOrder_ = -1;

    IRVoxelEditor::g_skeletonList = IRPrefab::Widget::makeList(
        ivec2(kSkeletonPanelPos.x + 4, kSkeletonPanelPos.y + 18),
        ivec2(112, 52),
        {},
        -1,
        13
    );
    IRVoxelEditor::g_jointRenameInput = IRPrefab::Widget::makeTextInput(
        ivec2(kSkeletonPanelPos.x + 4, kSkeletonPanelPos.y + 74),
        ivec2(82, 14),
        "",
        24
    );
    IRVoxelEditor::g_jointRenameBtn = IRPrefab::Widget::makeButton(
        ivec2(kSkeletonPanelPos.x + 90, kSkeletonPanelPos.y + 74),
        ivec2(26, 14),
        "REN"
    );
    IRVoxelEditor::g_jointReparentInput = IRPrefab::Widget::makeTextInput(
        ivec2(kSkeletonPanelPos.x + 4, kSkeletonPanelPos.y + 92),
        ivec2(82, 14),
        "-1",
        4
    );
    IRVoxelEditor::g_jointReparentBtn = IRPrefab::Widget::makeButton(
        ivec2(kSkeletonPanelPos.x + 90, kSkeletonPanelPos.y + 92),
        ivec2(26, 14),
        "PAR"
    );

    // Hover-help panel — docks below the panel stack and shows the help text of
    // whatever panel/control is hovered (drawn by the EditorHelpRender system).
    IRPrefab::Widget::makePanel(
        IRVoxelEditor::kHelpPanelPos,
        IRVoxelEditor::kHelpPanelSize,
        "HELP"
    );
    IRVoxelEditor::g_helpEntries = {
        {g_editor.palettePanel_, "PALETTE: click a swatch to set the active paint color."},
        {IRVoxelEditor::g_scrubberSlider, "FRAME: drag to scrub frames (Left/Right keys too)."},
        {IRVoxelEditor::g_fpsSlider, "FPS: animation playback speed (1-30)."},
        {IRVoxelEditor::g_layerPanel, "LAYERS: edit-layer stack. Keys K [ ] H also work."},
        {IRVoxelEditor::g_layerList, "LAYERS: click a layer row to make it active."},
        {IRVoxelEditor::g_layerVisCheckbox, "VISIBLE: toggle the active layer's visibility (H)."},
        {IRVoxelEditor::g_layerAddBtn, "ADD: create a new edit layer."},
        {IRVoxelEditor::g_layerDelBtn, "DEL: remove the active edit layer."},
        {IRVoxelEditor::g_bakePanel, "BAKE: pick a shape, set P1/P2, then BAKE the active entity."},
        {IRVoxelEditor::g_bakeShapeList, "SHAPE: choose the SDF primitive to voxelize."},
        {IRVoxelEditor::g_bakeParam1Slider, "P1: primary shape parameter (size / radius)."},
        {IRVoxelEditor::g_bakeParam2Slider, "P2: secondary shape parameter."},
        {IRVoxelEditor::g_bakeButton, "BAKE: voxelize the selected shape into the active entity."},
        {IRVoxelEditor::g_bonePaint.bonePanel_,
         "BONE: click a swatch to pick a bone; N toggles paint."},
        {IRVoxelEditor::g_skeletonPanel, "SKELETON: click a joint to select it; REN/PAR edit it."},
        {IRVoxelEditor::g_skeletonList, "JOINTS: click a row to select that joint as active bone."},
        {IRVoxelEditor::g_jointRenameBtn, "REN: rename the selected joint to the text at left."},
        {IRVoxelEditor::g_jointReparentBtn, "PAR: reparent selected joint to the index at left."},
    };

    // Ghost preview entity for drag-fill. Invisible until
    // a left-drag starts; the placeEraseSystem sets flags_/params_/pos_ each HELD
    // frame and hides it again on RELEASED.
    IRVoxelEditor::g_fillTool.ghostEntity_ = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(0.5f, 0.5f, 0.5f, 0.0f),
            Color{240, 240, 240, 200}
        }
    );
    IREntity::getComponent<C_ShapeDescriptor>(IRVoxelEditor::g_fillTool.ghostEntity_).flags_ =
        IRMath::SDF::SHAPE_FLAG_NONE;

    // Fill-mode status label — top-left of the GUI canvas. Updated each frame
    // by placeEraseSystem to show the active mode (BOX / LINE / FACE) and which
    // symmetry axes are active so the user can see modifier state at a glance.
    IRVoxelEditor::g_fillModeLabel = IRPrefab::Widget::makeLabel(ivec2(4, 4), "BOX");

    // GUI-test assertions (P3, #1796) — populated here (not at the constexpr
    // shot table) because they reference runtime widget EntityIds. The
    // hover-export singleton (one per world) lets HOVERS read the topmost
    // hovered widget via WIDGET_INPUT::endTick. The scripted GUI-assert shot
    // parks the cursor on the layer list and clicks; the pick shot casts a ray
    // onto the scene. PICKS_VOXEL's expected voxel is the regression baseline
    // for screen→world picking alignment.
    IRPrefab::Widget::makeGuiHoverState();
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kGuiAssertShotIndex] = {
        IRPrefab::GuiTest::hovers(IRVoxelEditor::g_layerList, "layer_list_hover"),
        IRPrefab::GuiTest::clickFires(IRVoxelEditor::g_layerList, "layer_list_click"),
        IRPrefab::GuiTest::checkbox(IRVoxelEditor::g_layerVisCheckbox, true, "layer_visible"),
        IRPrefab::GuiTest::sliderValue(
            IRVoxelEditor::g_fpsSlider,
            IRVoxelEditor::g_anim.fps_,
            0.5f,
            "fps_value"
        ),
    };
    // The pick_voxel baseline is a specific 16³-scene voxel; it only holds at the
    // default size, so skip it under --scene-size (the probe_map assertions below
    // cover mapping accuracy at any size). Leaving the table empty makes
    // onGuiAssertFrame skip the shot cleanly.
    if (IRVoxelEditor::g_editableSceneSize == IRVoxelEditor::kDefaultEditableSceneSize) {
        constexpr ivec3 kScenePickExpected{-1, -1, -1};
        IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kPickVoxelShotIndex] = {
            IRPrefab::GuiTest::picksVoxel(kScenePickExpected, "scene_pick"),
        };
    }
    // Phase 0 probe 2 (#766): each probe-map shot asserts the ray landed on the
    // target cell's iso COLUMN (not the exact voxel) — mapping accuracy is a 2D
    // screen-projection property, and the seed scene's rig geometry can occlude
    // the ground cell along the aimed column. Target = scene origin + local cell
    // (integers, so exact). onGuiAssertFrame supplies the matching cursor pixel.
    for (int i = 0; i < IRVoxelEditor::kProbeMapCount; ++i) {
        const ivec3 target =
            ivec3(IRVoxelEditor::g_editableSceneOrigin) + IRVoxelEditor::probeGroundCell(i);
        IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kProbeMapShotStart + i] = {
            IRPrefab::GuiTest::picksIsoColumn(target, "probe_map"),
        };
    }
}
