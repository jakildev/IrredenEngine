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
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/array_transforms.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_joint_name.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/rig_bridge.hpp>
#include <irreden/voxel/sdf_fill.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/common/rotation_mode.hpp>

// Gizmo primitives
#include <irreden/render/gizmo.hpp>

// Picking + ray-hit struct
#include <irreden/render/picking.hpp>

// Widget framework
#include <irreden/render/widgets.hpp>

// GUI-test assertions
#include <irreden/render/gui_test_assertions.hpp>

// Systems
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/render/systems/system_gate_voxel_sets_by_lod.hpp>
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
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
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
#include <irreden/render/systems/system_widget_lua_dispatch.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_color_swatch.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>

// Camera prefab namespace (Z-yaw API)
#include <irreden/render/camera.hpp>

// Registry-driven command help overlay
#include <irreden/render/help_overlay.hpp>

// Frame-based animation state
#include "animation.hpp"

// The ANIM panel's slider geometry, shared with the session builder so a
// scripted drag aims at the live layout.
#include "anim_panel.hpp"
#include "array_panel.hpp"
#include "lod_panel.hpp"

#include "editor_layer_manager.hpp"

// Paint palette colours + the GUI-canvas geometry of the swatch grid, shared
// with the session builder so a scripted swatch click aims at the live layout.
#include "palette.hpp"

// Creation-module host (--module) and the RECIPES / module-panel geometry.
#include "bake_panel.hpp"
#include "editor_lua_host.hpp"
#include "recipes_panel.hpp"

// COMPONENTS panel geometry; the records it edits live on the entity scene.
#include "components_panel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

// Symmetry modes
#include "symmetry.hpp"

// Authoring sessions — recipes of editor gestures compiled into
// scripted input and replayed against the live UI by the GUI-test harness.
#include "editor_picking.hpp"
#include "entity_scene.hpp"
#include "sessions.hpp"

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

// Scene + palette config, kept as named constants so the editor never
// inlines a hardcoded dimension: the size is configurable per scene.
//
// The editable grid dimensions are runtime-configurable via --scene-size W H D
// (the ant needs 20³, the tree ~26 tall). g_editableSceneSize /
// g_editableSceneOrigin default to the 16³ scene and are overwritten
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
// Per-bone display colors for the bone selector panel.
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

// Per-stroke undo record. One record per click — each single-voxel
// place/erase event commits one record — but the unit is a stroke so
// drag-paint can fold many edits into one record without rewiring undo
// replay.
struct UndoEdit {
    IREntity::EntityId voxelSet_;
    ivec3 localIdx_;
    C_Voxel prev_;
};

struct UndoRecord {
    std::vector<UndoEdit> edits_;
    std::vector<IREntity::EntityId> createdParts_;
    std::optional<RemovedEditorPart> removedPart_;

    std::size_t byteSize() const {
        return sizeof(UndoEdit) * edits_.size() +
               sizeof(IREntity::EntityId) * createdParts_.size() +
               (removedPart_ ? removedPart_->byteSize() : 0);
    }
};

// Per-stroke byte budget with whole-stroke eviction. One mebibyte
// is enough headroom for a 1024-edit stroke (24 KiB) to live ~40 strokes
// deep before the oldest evicts. Applied per frame: each animation frame
// has its own independent undo stack capped at this limit; total in-memory
// undo cost is at most kUndoByteBudget × frameCount.
constexpr std::size_t kUndoByteBudget = 1u << 20;

// Reserve capacity for the per-stroke edits vector at stroke-begin so
// the per-voxel write hot path doesn't allocate. The brush is
// single-voxel; the reserve is a high-water-mark sized for a drag-paint
// stroke.
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

// Bone-paint mode state. N toggles the mode; while active,
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

// Frame-based animation state. Each VoxelFrame snapshots
// the editable target's voxel pool span; switchToFrame swaps the live
// voxels in and out. Lives at module scope (not in EditorState) because
// the playback system reads it from a stateless lambda — keeping it as
// a free global avoids threading a pointer through SystemParams just to
// reach the same address every frame.
AnimationState g_anim;

// Authoring session selected by --gui-session. When one is
// active it replaces the standing GUI-test shot table with the recipe's
// segments, and the scene is built without the demo furniture (see
// initEntities). Both live at module scope because main() resolves them right
// after the arg parse, initSystems hands the shots to the harness, and the
// per-frame assert callback reads them back.
Session::Id g_sessionId = Session::Id::NONE;
Session::Recipe g_session;

// The creation module --module loaded, and the editor UI built for it: the
// RECIPES panel and one docked panel per IREditor.registerPanel. All empty
// without --module.
ModuleHost g_moduleHost;
IRSystem::SystemId g_widgetLuaDispatchId = IRSystem::kNullSystemId;

struct DockedModulePanel {
    IREntity::EntityId panel_ = IREntity::kNullEntity;
    ivec2 pos_ = ivec2(0);
};
std::vector<DockedModulePanel> g_dockedModulePanels;

IREntity::EntityId g_recipesPanel = IREntity::kNullEntity;
IREntity::EntityId g_recipeList = IREntity::kNullEntity;
IREntity::EntityId g_recipeApplyBtn = IREntity::kNullEntity;
std::array<IREntity::EntityId, kMaxRecipeParams> g_recipeSliders{};
// Recipe the parameter sliders currently describe; -1 before the first sync.
int g_slidersRecipe = -1;

// The COMPONENTS panel (initComponentsUi). Not built when the process has no
// component to offer: no module component and no engine prefab factory.
IREntity::EntityId g_componentsPanel = IREntity::kNullEntity;
IREntity::EntityId g_componentList = IREntity::kNullEntity;
IREntity::EntityId g_componentRootToggle = IREntity::kNullEntity;
IREntity::EntityId g_componentAttachBtn = IREntity::kNullEntity;
IREntity::EntityId g_componentDetachBtn = IREntity::kNullEntity;
IREntity::EntityId g_componentStatusLabel = IREntity::kNullEntity;
// Component list row r attaches g_componentNames[r].
std::vector<std::string> g_componentNames;

// One row of the field area. `input_` is a text input, a checkbox
// (`checkbox_`), or null for a read-only field; `field_` indexes the record's
// fields, or is -1 for an ENGINE record's overrides.
struct ComponentFieldRow {
    IREntity::EntityId label_ = IREntity::kNullEntity;
    IREntity::EntityId input_ = IREntity::kNullEntity;
    bool checkbox_ = false;
    int field_ = -1;
    bool wasFocused_ = false;
};
std::vector<ComponentFieldRow> g_componentFieldRows;
// The record and entity the field rows were built for; a change rebuilds them.
std::string g_componentFieldRowsKey;
// The page of that record's fields the rows show, and the pager that turns it;
// the pager exists only while the record has more than one page.
int g_componentFieldPage = 0;
IREntity::EntityId g_componentPagePrevBtn = IREntity::kNullEntity;
IREntity::EntityId g_componentPageNextBtn = IREntity::kNullEntity;
IREntity::EntityId g_componentPageLabel = IREntity::kNullEntity;

namespace {

constexpr float kRotationSensitivity = 0.004f;

SymmetryState g_symmetry;

// Erase-fill mode. When ON, the left-click place / box / line /
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
    // Alt (place on the far side of the hit face) latched at PRESS, so letting
    // the modifier go mid-drag cannot resolve the stroke's start and end cells
    // in two different frames — see editTargetCell.
    bool dragInverted_ = false;
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

// Scripted GUI-assert click: park the cursor over the LAYERS-panel
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

// Scripted scene-pick: move the cursor over the 3D scene (right of
// the left-column GUI panels), so PICKS_VOXEL casts a ray onto a scene voxel —
// the regression net for the screen→world picking alignment.
constexpr IRVideo::GuiInputEvent kPickVoxelEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, IRMath::ivec2(800, 450)},
};

// --- Mechanism probes ----------------------------------------------
// Prove the auto-authoring premise the session infrastructure rests on:
// keyboard→command dispatch, world→screen click mapping, and the A/D binding
// overload — all through the live GUI harness on the seed scene.
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
// Ctrl+S again, but Ctrl comes back up BEFORE S does. The press was
// shadowed, so neither pan half may fire — and the release frame, which sees no
// modifier at all, is the one the old frame-local rule could not reach.
constexpr IRVideo::GuiInputEvent kProbeSaveCtrlFirstEvents[] = {
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
     IRInput::kKeyButtonLeftControl},
    {4,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
};

// The mirror case: a BARE S press starts the pan, then Ctrl arrives mid-hold.
// The pan must actually run while S is down and must stop on release. A
// `blockedModifiers` mask on the pair is unbalanced in exactly this direction —
// live Ctrl rejects the release row and strands the start — so this shot is the
// lock that keeps the S pair riding the suite unmasked.
constexpr IRVideo::GuiInputEvent kProbePanThenCtrlEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonS},
    {2,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonLeftControl},
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

// F1 toggles the help overlay. The overlay builds its text lazily on
// the first open, so the open shot is the only place an assertion can read what
// it actually rendered; a second press on the following shot closes it again so
// the probe shots below run against a hidden overlay and an unpainted GUI
// canvas.
constexpr IRVideo::GuiInputEvent kHelpOverlayOpenEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonF1},
    {1,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonF1},
};

constexpr IRVideo::GuiInputEvent kHelpOverlayCloseEvents[] = {
    {0,
     IRVideo::GuiInputEvent::Type::PRESS,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonF1},
    {1,
     IRVideo::GuiInputEvent::Type::RELEASE,
     IRMath::ivec2(0),
     IRMath::vec2(0.0f),
     IRInput::kKeyButtonF1},
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

// --- Erase-fill mode probe ---------------------------------
// Verifies the erase-fill toggle through the live UI, occlusion-free: synthetic
// V flips g_eraseMode ON, and a capture-frame PREDICATE assertion
// (evaluateEraseModeLabel) checks the fill-mode status label the place/erase
// system repaints each frame now reads "ERASE BOX". This exercises the whole
// control path — synthetic key → command dispatch → g_eraseMode → status label —
// without a scene click, so it is immune to the seed scene's occlusion.
//
// Why not a scripted-erase-removes-a-voxel check here: the standing shot table
// runs against the demo scene, whose reference shapes and posed starter rig sit
// between the camera and the editable ground plane. The functional carve check
// lives in the Part 2c `drag_probe` session instead, which authors on a scene
// built without that furniture. The two V key events carry no pixel; the shot
// leaves erase mode ON (harmless — the trailing save / A-D probes don't read it).
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

// The fill-mode status label's current text, read by the erase-fill
// probe. Returns "" when the label isn't built yet.
std::string fillModeLabelText();

// GUI-test shot table covering stable render framings plus the scripted-click
// shots. Superset of the previous kShots[] — render-verify labels still match.
// The k*ShotIndex constants below select the assertion-bearing shots.
constexpr IRVideo::GuiTestShot kGuiTestShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_idle"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_palette_click"}, kPaletteClickEvents, 3},
    {{0.75f, IRMath::vec2(0.0f), 0.0f, "editor_zoom_out"}, nullptr, 0},
    {{1.5f, IRMath::vec2(0.0f), 0.0f, "editor_zoom_in"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_gui_assert"}, kGuiAssertEvents, 3},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_pick_voxel"}, kPickVoxelEvents, 1},
    // Help-overlay open/closed pair, ahead of the probe shots so the
    // close half restores the hidden state they expect.
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_help_overlay_open"}, kHelpOverlayOpenEvents, 2},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_help_overlay_closed"}, kHelpOverlayCloseEvents, 2},
    // Mechanism probes, appended after the stable shots so their indices
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
    // Erase-fill probe — placed after the read-only map shots
    // (which leave the seed scene intact) and before the state-mutating save /
    // A-D probes, so it erases from the still-seeded editable set.
    {{2.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_erase"},
     kProbeEraseEvents,
     kProbeEraseNumEvents},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_save"}, kProbeSaveEvents, 4},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_ad"}, kProbeADEvents, 2},
    // Pair-balance probes. Last, because both pan the camera: the shots
    // above read the scene through a camera these would otherwise have moved.
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_save_ctrl_first"},
     kProbeSaveCtrlFirstEvents,
     4},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "editor_probe_pan_then_ctrl"}, kProbePanThenCtrlEvents, 4},
};
constexpr int kNumGuiTestShots = static_cast<int>(sizeof(kGuiTestShots) / sizeof(kGuiTestShots[0]));
constexpr int kGuiAssertShotIndex = 4;
constexpr int kPickVoxelShotIndex = 5;
// The help-overlay pair. Its assertions read state only an OPEN overlay has
// (the text it built, the glyph commands it batched), so they cannot ride the
// input-free idle shot.
constexpr int kHelpOverlayOpenShotIndex = kPickVoxelShotIndex + 1;
constexpr int kHelpOverlayClosedShotIndex = kHelpOverlayOpenShotIndex + 1;
// Mechanism / erase-fill probe shot indices. Map shots occupy
// [start, start+count); the erase probe, then the dispatch and overload shots
// follow. Derived from the preceding shot index so they track any reordering of
// the stable shots.
constexpr int kProbeMapShotStart = kHelpOverlayClosedShotIndex + 1;
constexpr int kProbeEraseShotIndex = kProbeMapShotStart + kProbeMapCount;
constexpr int kProbeSaveShotIndex = kProbeEraseShotIndex + 1;
constexpr int kProbeADShotIndex = kProbeSaveShotIndex + 1;
constexpr int kProbeSaveCtrlFirstShotIndex = kProbeADShotIndex + 1;
constexpr int kProbePanThenCtrlShotIndex = kProbeSaveCtrlFirstShotIndex + 1;
static_assert(
    kProbePanThenCtrlShotIndex + 1 == kNumGuiTestShots,
    "Phase 0 / Part 2b (#766) and pair-balance (#3273) probe shots must be the "
    "final kGuiTestShots entries"
);

// GUI-test assertion tables. Filled in initEntities once the widget
// entities exist — assertions reference runtime EntityIds, so unlike the shot
// table they can't be constexpr. Index-aligned with kGuiTestShots.
// g_guiAssertLatch is the caller-owned latch the harness's onAssertFrame_
// callback threads through onFrame (CLICK_FIRES latches the one-frame pulse).
IRPrefab::GuiTest::LatchState g_guiAssertLatch;
std::vector<IRPrefab::GuiTest::Assertion> g_shotAssertions[kNumGuiTestShots];

// Erase-fill probe check: after synthetic V toggled erase mode
// ON, the fill-mode status label the place/erase system repaints each frame
// must read "ERASE BOX". Exercises the whole control path — synthetic key →
// command dispatch → g_eraseMode → status label — with no scene click, so it is
// immune to the seed scene's occlusion.
bool evaluateEraseModeLabel(const void *, std::string &actual) {
    const std::string labelText = fillModeLabelText();
    actual =
        "eraseMode=" + std::string(g_eraseMode ? "ON" : "OFF") + " label=\"" + labelText + "\"";
    return g_eraseMode && labelText == "ERASE BOX";
}

// The key column the help overlay is expected to render for each
// modifier-bearing binding. The overlay composes that column as
// `modifierString(requiredModifiers) + keyButtonToString(button)`, so a chord
// whose mask is left at none advertises the bare key — Ctrl+Z and bare Z would
// both read "Z", and the two save / two load bindings would read "S" and "O"
// twice. Each binding's real mask is what makes its row correct; this is what
// keeps it correct.
//
// Read out of the overlay's own `builtText()` on the F1 shot, NOT rebuilt from
// `getCommandRegistrations()`: a registry-side check only repeats the formatter
// expression, so it passes whether or not this creation has an overlay at all,
// whether it is open, and whether its text stage ever queued a glyph — none of
// which is what "the bindings appear when opened" asks.
struct ChordRow {
    const char *chord_;
    const char *name_;
};
constexpr ChordRow kExpectedChordRows[] = {
    {"CTRL+Z", "UNDO"},
    {"Z", "MIRROR Z"},
    {"CTRL+S", "SAVE SCENE"},
    {"SHIFT+CTRL+S", "SAVE RIG"},
    {"CTRL+O", "LOAD SCENE"},
    {"SHIFT+CTRL+O", "LOAD RIG"},
};
constexpr int kNumExpectedChordRows =
    static_cast<int>(sizeof(kExpectedChordRows) / sizeof(kExpectedChordRows[0]));

// Splits one overlay row into its key column and command name.
// `System<HELP_OVERLAY>::buildText` lays a row out as
// `<key><pad to a fixed width> <NAME>[ - <DESCRIPTION>]`, and a key column never
// contains a space, so the first space ends it and the name runs to the " - "
// separator. Parsed rather than re-derived from kHelpOverlayBindingColumnChars:
// a widened gutter is a layout change, not a regression in what this asserts.
// Returns false for the header and blank lines, which have no key column.
bool splitOverlayRow(const std::string &line, std::string &key, std::string &name) {
    const std::size_t keyEnd = line.find(' ');
    if (keyEnd == 0 || keyEnd == std::string::npos)
        return false;
    const std::size_t nameStart = line.find_first_not_of(' ', keyEnd);
    if (nameStart == std::string::npos)
        return false;
    const std::size_t descStart = line.find(" - ", nameStart);
    key = line.substr(0, keyEnd);
    name = line.substr(
        nameStart,
        descStart == std::string::npos ? std::string::npos : descStart - nameStart
    );
    return true;
}

bool evaluateChordOverlayRows(const void *, std::string &actual) {
    const std::string text = IRPrefab::HelpOverlay::builtText();
    if (text.empty()) {
        actual = "overlay text is empty — never opened, or HELP_OVERLAY is not registered";
        return false;
    }

    std::string rendered[kNumExpectedChordRows];
    for (std::string &row : rendered)
        row = "<absent>";
    for (std::size_t lineStart = 0; lineStart < text.size();) {
        const std::size_t lineEnd = text.find('\n', lineStart);
        const std::size_t lineLength =
            lineEnd == std::string::npos ? text.size() - lineStart : lineEnd - lineStart;
        const std::string line = text.substr(lineStart, lineLength);
        lineStart += lineLength + 1;
        std::string key;
        std::string name;
        if (!splitOverlayRow(line, key, name))
            continue;
        for (int i = 0; i < kNumExpectedChordRows; ++i) {
            if (name == kExpectedChordRows[i].name_)
                rendered[i] = key;
        }
    }

    std::string wrong;
    for (int i = 0; i < kNumExpectedChordRows; ++i) {
        if (rendered[i] == kExpectedChordRows[i].chord_)
            continue;
        if (!wrong.empty())
            wrong += ", ";
        wrong += std::string(kExpectedChordRows[i].name_) + " renders \"" + rendered[i] +
                 "\" want \"" + kExpectedChordRows[i].chord_ + "\"";
    }
    actual = wrong.empty() ? "all six chord rows present in the opened overlay's text" : wrong;
    return wrong.empty();
}

// Visibility is the flag the F1 command flips; the glyph count is the evidence
// the overlay acted on it. `dispatchGuiText` drains the command vector as it
// uploads, so `lastGlyphCommandCount()` is the only after-the-fact proof the
// overlay queued geometry rather than merely believing itself visible — and it
// is exactly 0 on the closed shot, which is what shows the text is gone from the
// canvas rather than still painted under a cleared flag.
bool evaluateOverlayVisibility(const void *context, std::string &actual) {
    const bool expected = *static_cast<const bool *>(context);
    const bool visible = IRPrefab::HelpOverlay::isVisible();
    actual = visible ? "visible" : "hidden";
    return visible == expected;
}

bool evaluateOverlayGlyphsBatched(const void *context, std::string &actual) {
    const bool expectGlyphs = *static_cast<const bool *>(context);
    const int count = IRPrefab::HelpOverlay::lastGlyphCommandCount();
    actual = "glyphCommands=" + std::to_string(count);
    return expectGlyphs ? count > 0 : count == 0;
}

constexpr bool kOverlayExpectVisible = true;
constexpr bool kOverlayExpectHidden = false;

// Camera-velocity balance across the three chord probes below. S is both "pan
// the camera down" and the Ctrl+S save chord, so the pan pair's two halves must
// fire together or the camera pans forever — `CommandManager` admits a bare
// start/end pair once at the press for exactly that reason, and these
// assertions are what hold it to the promise from the creation side.
//
// Peak pan speed seen during the pan-then-Ctrl probe's hold, latched by
// `onGuiAssertFrame` on that shot's non-capture frames. The capture frame reads
// the camera's velocity long after both halves have drained, so "the pan
// stopped" is only meaningful next to proof that it ever started.
float g_probePanPeakSpeed = 0.0f;

// Shared body of the three balance predicates: the S pan pair accumulates into
// `C_Velocity2DIso` with `-=` on press and `+=` on release, so a leftover half
// reads as a stuck +/-20 here.
bool cameraVelocityIsZero(std::string &actual) {
    const IRMath::vec2 velocity =
        IREntity::getComponent<IRComponents::C_Velocity2DIso>("camera").velocity_;
    actual = "camera velocity = (" + std::to_string(velocity.x) + ", " +
             std::to_string(velocity.y) + ")";
    return IRMath::abs(velocity.x) < 0.001f && IRMath::abs(velocity.y) < 0.001f;
}

bool evaluateCameraVelocityBalanced(const void *, std::string &actual) {
    return cameraVelocityIsZero(actual);
}

// Ctrl released before S. Same expectation, different ordering: the
// shadowed press must not leave a live release behind even though the release
// frame itself sees no modifier.
bool evaluateCtrlFirstReleaseBalanced(const void *, std::string &actual) {
    return cameraVelocityIsZero(actual);
}

// Bare start, Ctrl mid-hold. Both halves of the claim are asserted: the
// pan ran (peak > 0 while S was down) and then stopped (velocity 0 after).
bool evaluatePanThenCtrlBalanced(const void *, std::string &actual) {
    const bool stopped = cameraVelocityIsZero(actual);
    actual = "peak pan speed = " + std::to_string(g_probePanPeakSpeed) + ", " + actual;
    return g_probePanPeakSpeed > 0.001f && stopped;
}

// Per-frame driver for an authoring session. Resolves this
// segment's cursor aims against the shot's live camera — the world→screen
// mapping reads zoom / iso offset / letterbox, so the pixel cannot be baked at
// recipe-build time — then evaluates the segment's occupancy assertions. The
// write is idempotent across the shot's frames and lands before the harness's
// event phase on the same tick, so the scheduled MOVE injects the fresh pixel
// (same ordering the probe-map shots rely on).
void onSessionAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (shotIndex < 0 || shotIndex >= static_cast<int>(g_session.segments_.size()))
        return;
    Session::Segment &segment = g_session.segments_[shotIndex];
    for (const Session::AimFixup &aim : segment.aims_) {
        segment.events_[static_cast<std::size_t>(aim.eventIndex_)].screenPx_ =
            IRRender::worldPos3DToMouseScreenPxExact(aim.worldPoint_);
    }
    // Widget aims resolve through the GUI-canvas mapping instead — the canvas is
    // sized from the live framebuffer, so a swatch's screen pixel is no more
    // bakeable at recipe-build time than a scene voxel's is.
    for (const Session::GuiAimFixup &aim : segment.guiAims_) {
        segment.events_[static_cast<std::size_t>(aim.eventIndex_)].screenPx_ =
            IRRender::guiTrixelToScreenPx(aim.guiTrixel_);
    }
    IRPrefab::GuiTest::onFrame(
        g_guiAssertLatch,
        shotIndex,
        isCaptureFrame,
        segment.label_.c_str(),
        segment.assertions_.data(),
        static_cast<int>(segment.assertions_.size())
    );
}

// Forwarder wired to IRVideo::GuiTestConfig::onAssertFrame_. The harness owns
// input + capture timing in engine/video; this hands each frame to the prefab
// evaluator with the shot's assertion table (engine/video can't see widgets).
void onGuiAssertFrame(int shotIndex, bool isCaptureFrame) {
    if (g_sessionId != Session::Id::NONE) {
        onSessionAssertFrame(shotIndex, isCaptureFrame);
        return;
    }
    if (shotIndex < 0 || shotIndex >= kNumGuiTestShots)
        return;
    // Fill this probe-map shot's cursor move with the
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
    // Sample the live pan speed while S is down, before the capture frame
    // reads the settled velocity — `evaluatePanThenCtrlBalanced` asserts both.
    if (shotIndex == kProbePanThenCtrlShotIndex && !isCaptureFrame) {
        const IRMath::vec2 velocity =
            IREntity::getComponent<IRComponents::C_Velocity2DIso>("camera").velocity_;
        g_probePanPeakSpeed = IRMath::max(
            g_probePanPeakSpeed,
            IRMath::max(IRMath::abs(velocity.x), IRMath::abs(velocity.y))
        );
    }
    const auto &assertions = g_shotAssertions[shotIndex];
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

// Animation scrubber and FPS slider entity IDs. Initialized in
// initEntities; accessed by initSystems lambdas and switchToFrame.
IREntity::EntityId g_scrubberSlider = IREntity::kNullEntity;
IREntity::EntityId g_fpsSlider = IREntity::kNullEntity;

// Layer panel widget entity IDs. Initialized in initEntities.
IREntity::EntityId g_layerPanel = IREntity::kNullEntity;
IREntity::EntityId g_layerList = IREntity::kNullEntity;
IREntity::EntityId g_layerVisCheckbox = IREntity::kNullEntity;
IREntity::EntityId g_layerAddBtn = IREntity::kNullEntity;
IREntity::EntityId g_layerDelBtn = IREntity::kNullEntity;

EntityScene g_entityScene;
// Process start on the filesystem clock: a session's manifest check accepts
// only a file written after it.
const std::filesystem::file_time_type g_runStartFileTime =
    std::filesystem::file_time_type::clock::now();
bool g_entitySceneMode = false;
IREntity::EntityId g_partsPanel = IREntity::kNullEntity;
IREntity::EntityId g_partsList = IREntity::kNullEntity;
IREntity::EntityId g_partRemoveButton = IREntity::kNullEntity;
std::vector<const EditorPart *> g_rotationGroupScratch;
std::vector<C_VoxelSetNew *> g_rotationSetScratch;
IREntity::EntityId g_arrayPanel = IREntity::kNullEntity;
IREntity::EntityId g_arrayTypeList = IREntity::kNullEntity;
IREntity::EntityId g_arrayCountSlider = IREntity::kNullEntity;
IREntity::EntityId g_arrayDistanceSlider = IREntity::kNullEntity;
IREntity::EntityId g_arrayStepYSlider = IREntity::kNullEntity;
IREntity::EntityId g_arrayStepZSlider = IREntity::kNullEntity;
IREntity::EntityId g_arrayYawSlider = IREntity::kNullEntity;
IREntity::EntityId g_arrayApplyButton = IREntity::kNullEntity;

// LOD panel widgets (lod_panel.hpp). The band sliders edit the selected part;
// the tier slider and FOLLOW ZOOM drive EntityScene::setTierOverride and never
// the camera zoom.
IREntity::EntityId g_lodPanel = IREntity::kNullEntity;
IREntity::EntityId g_lodFineSlider = IREntity::kNullEntity;
IREntity::EntityId g_lodCoarseSlider = IREntity::kNullEntity;
IREntity::EntityId g_lodTierSlider = IREntity::kNullEntity;
IREntity::EntityId g_lodFollowCheckbox = IREntity::kNullEntity;

IRRender::LodLevel lodLevelFromSlider(IREntity::EntityId slider) {
    const float value =
        IRMath::clamp(IRPrefab::Widget::sliderValue(slider), kLodTierSliderMin, kLodTierSliderMax);
    return static_cast<IRRender::LodLevel>(IRMath::roundHalfUp(value));
}

void syncLodBandSliders() {
    if (g_lodFineSlider == IREntity::kNullEntity) {
        return;
    }
    const int selected = g_entityScene.selectedIndex();
    const bool hasPart = selected >= 0;
    IRPrefab::Widget::setDisabled(g_lodFineSlider, !hasPart);
    IRPrefab::Widget::setDisabled(g_lodCoarseSlider, !hasPart);
    if (!hasPart) {
        return;
    }
    const EditorPart &part = g_entityScene.parts()[static_cast<std::size_t>(selected)];
    IRPrefab::Widget::setSliderValue(g_lodFineSlider, static_cast<float>(part.lodMax_));
    IRPrefab::Widget::setSliderValue(g_lodCoarseSlider, static_cast<float>(part.lodMin_));
}

// nullopt follows the camera-zoom tier. The panel mirrors the state so a
// hotkey and the widgets never disagree.
void setLodTierPin(std::optional<IRRender::LodLevel> tier) {
    g_entityScene.setTierOverride(tier);
    if (g_lodFollowCheckbox == IREntity::kNullEntity) {
        return;
    }
    IRPrefab::Widget::setCheckboxState(g_lodFollowCheckbox, !tier.has_value());
    if (tier) {
        IRPrefab::Widget::setSliderValue(g_lodTierSlider, static_cast<float>(*tier));
    }
}

// Steps the pinned tier by @p delta (negative = finer). From follow-zoom the
// step starts at the live zoom tier.
void stepLodTierPin(int delta) {
    const IRRender::LodLevel from =
        g_entityScene.tierOverride().value_or(IRRender::getActiveLodLevel());
    const int stepped = IRMath::clamp(
        static_cast<int>(from) + delta,
        static_cast<int>(kLodTierSliderMin),
        static_cast<int>(kLodTierSliderMax)
    );
    setLodTierPin(static_cast<IRRender::LodLevel>(stepped));
}

IREntity::EntityId g_partModeDropdown = IREntity::kNullEntity;
int g_modePreviewBaseCanvasCount = 0;

int rotationModeIndex(IRComponents::RotationMode mode) {
    switch (mode) {
    case IRComponents::RotationMode::GRID:
        return 0;
    case IRComponents::RotationMode::DETACHED:
        return 1;
    case IRComponents::RotationMode::DETACHED_REVOXELIZE:
        return 2;
    }
    return 0;
}

IRComponents::RotationMode rotationModeFromIndex(int index) {
    switch (index) {
    case 1:
        return IRComponents::RotationMode::DETACHED;
    case 2:
        return IRComponents::RotationMode::DETACHED_REVOXELIZE;
    default:
        return IRComponents::RotationMode::GRID;
    }
}

IRComponents::RotationMode nextRotationMode(IRComponents::RotationMode mode) {
    switch (mode) {
    case IRComponents::RotationMode::GRID:
        return IRComponents::RotationMode::DETACHED;
    case IRComponents::RotationMode::DETACHED:
        return IRComponents::RotationMode::DETACHED_REVOXELIZE;
    case IRComponents::RotationMode::DETACHED_REVOXELIZE:
        return IRComponents::RotationMode::GRID;
    }
    return IRComponents::RotationMode::GRID;
}

void syncPartModeDropdown() {
    if (g_partModeDropdown == IREntity::kNullEntity) {
        return;
    }
    const EditorPart *part = g_entityScene.selectedPart();
    IRPrefab::Widget::setDropdownSelectedIndex(
        g_partModeDropdown,
        part != nullptr ? rotationModeIndex(part->mode_) : 0
    );
}

void stageRotationMode(
    IREntity::EntityId entity, IRComponents::RotationMode mode, bool persistPart = false
) {
    IREntity::getEntityManager().stageStructuralChange([entity, mode, persistPart]() {
        if (IREntity::entityExists(entity)) {
            IRPrefab::RotationMode::setMode(entity, mode);
            if (persistPart) {
                IRMath::ivec2 canvasSize{0};
                if (auto canvas = IREntity::getComponentOptional<C_EntityCanvas>(entity)) {
                    canvasSize = canvas.value()->canvasSize_;
                }
                g_entityScene.setPartRenderState(entity, mode, canvasSize);
            }
        }
    });
}

void createModePreviewTwin() {
    const EditorPart *part = g_entityScene.selectedPart();
    if (part == nullptr || part->kind_ != EditorPartKind::VOXEL_SET) {
        return;
    }
    const auto &source = IREntity::getComponent<C_VoxelSetNew>(part->entity_);
    const IRAsset::DenseVoxelSet dense = IRPrefab::DenseVoxel::fromComponent(source);
    C_LocalTransform transform = IREntity::getComponent<C_LocalTransform>(part->entity_);
    transform.translation_.x += static_cast<float>(source.size_.x * 2);
    const IREntity::EntityId preview = IREntity::createEntity(
        transform,
        IRPrefab::DenseVoxel::toComponent(dense),
        C_RotationMode{RotationMode::GRID},
        C_EditorReference{}
    );
    IREntity::setParent(preview, g_entityScene.root());
    g_entityScene.setPreviewEntity(preview);
    stageRotationMode(preview, nextRotationMode(part->mode_));
}

void setSelectedPartRotationMode(IRComponents::RotationMode mode) {
    EditorPart *part = g_entityScene.selectedPart();
    if (part == nullptr || part->kind_ != EditorPartKind::VOXEL_SET || part->mode_ == mode) {
        syncPartModeDropdown();
        return;
    }
    const bool refreshPreview = g_entityScene.previewEntity() != IREntity::kNullEntity;
    g_entityScene.destroyPreview();
    part->mode_ = mode;
    if (!IRPrefab::RotationMode::ownsEntityCanvas(mode)) {
        part->canvasSize_ = IRMath::ivec2{0};
    }
    stageRotationMode(part->entity_, mode, true);
    syncPartModeDropdown();
    if (refreshPreview) {
        createModePreviewTwin();
    }
}

void cycleSelectedPartRotationMode() {
    const EditorPart *part = g_entityScene.selectedPart();
    if (part != nullptr) {
        setSelectedPartRotationMode(nextRotationMode(part->mode_));
    }
}

void toggleModePreviewTwin() {
    if (g_entityScene.previewEntity() != IREntity::kNullEntity) {
        g_entityScene.destroyPreview();
        return;
    }
    createModePreviewTwin();
}

void clearUndoHistory() {
    g_editor.undoRecords_.clear();
    g_editor.undoTotalBytes_ = 0;
    g_editor.pendingStroke_.edits_.clear();
    g_editor.pendingStroke_.edits_.reserve(kUndoStrokeReserve);
    for (std::deque<UndoRecord> &records : g_editor.perFrameUndoStacks_) {
        records.clear();
    }
    std::fill(g_editor.perFrameUndoBytes_.begin(), g_editor.perFrameUndoBytes_.end(), 0);
}

void selectEditorPart(int index, bool createGizmos = true) {
    const IREntity::EntityId selected = g_entityScene.select(index, createGizmos);
    if (g_entityScene.previewSourceEntity() != IREntity::kNullEntity &&
        selected != g_entityScene.previewSourceEntity()) {
        g_entityScene.destroyPreview();
    }
    for (const EditorPart &part : g_entityScene.parts()) {
        if (part.entity_ == selected) {
            if (IREntity::getComponentOptional<C_EditorReference>(part.entity_)) {
                IREntity::removeComponent<C_EditorReference>(part.entity_);
            }
        } else if (!IREntity::getComponentOptional<C_EditorReference>(part.entity_)) {
            IREntity::setComponent(part.entity_, C_EditorReference{});
        }
    }
    const bool voxelPart = selected != IREntity::kNullEntity &&
                           IREntity::getComponentOptional<C_VoxelSetNew>(selected).has_value();
    g_editor.editableVoxelSet_ = voxelPart ? selected : IREntity::kNullEntity;
    g_sceneVoxelSetEntity = voxelPart ? selected : IREntity::kNullEntity;
    if (g_partsList != IREntity::kNullEntity) {
        auto &list = IREntity::getComponent<C_WidgetList>(g_partsList);
        list.items_.clear();
        for (const EditorPart &part : g_entityScene.parts()) {
            list.items_.push_back(part.id_);
        }
        IRPrefab::Widget::setListSelectedIndex(g_partsList, g_entityScene.selectedIndex());
    }
    if (g_partRemoveButton != IREntity::kNullEntity) {
        IRPrefab::Widget::setDisabled(g_partRemoveButton, selected == IREntity::kNullEntity);
    }
    syncLodBandSliders();
    syncPartModeDropdown();
}

void selectRelativeEditorPart(int offset) {
    if (!g_entitySceneMode || g_entityScene.parts().empty()) {
        return;
    }
    const int count = static_cast<int>(g_entityScene.parts().size());
    selectEditorPart((g_entityScene.selectedIndex() + offset + count) % count);
}

void addEditorVoxelPart() {
    if (!g_entitySceneMode) {
        g_entitySceneMode = true;
        clearUndoHistory();
        if (g_editor.editableVoxelSet_ != IREntity::kNullEntity) {
            IREntity::destroyEntity(g_editor.editableVoxelSet_);
        }
        g_entityScene.begin();
    }
    const IREntity::EntityId part =
        g_entityScene.addVoxelPart(g_editableSceneSize, g_editableSceneOrigin);
    auto &set = IREntity::getComponent<C_VoxelSetNew>(part);
    set.deactivateAll();
    selectEditorPart(g_entityScene.selectedIndex());
}

void clearEntitySceneForLoad() {
    clearUndoHistory();
    g_entityScene.clear();
    g_editor.editableVoxelSet_ = IREntity::kNullEntity;
    g_sceneVoxelSetEntity = IREntity::kNullEntity;
    if (g_partsList != IREntity::kNullEntity) {
        auto &list = IREntity::getComponent<C_WidgetList>(g_partsList);
        list.items_.clear();
        list.selectedIndex_ = -1;
    }
    syncLodBandSliders();
    syncPartModeDropdown();
}

// Fill-mode status label — top-left status bar updated each frame with the
// active fill mode (BOX / LINE / FACE), the erase-mode prefix, and active
// symmetry axes.
IREntity::EntityId g_fillModeLabel = IREntity::kNullEntity;

// Current text of the fill-mode status label (the erase probe reads it
// to verify the erase-mode toggle). Empty when the label isn't built yet.
std::string fillModeLabelText() {
    if (g_fillModeLabel == IREntity::kNullEntity)
        return {};
    return IREntity::getComponent<C_WidgetLabel>(g_fillModeLabel).text_;
}

// Parametric shape bake panel widget entity IDs.
IREntity::EntityId g_bakePanel = IREntity::kNullEntity;
IREntity::EntityId g_bakeShapeList = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam1Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam2Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeButton = IREntity::kNullEntity;

IRPrefab::Prefab::PrefabShapeDescription selectedShapeDescription() {
    const int selected = g_bakeShapeList != IREntity::kNullEntity
                             ? IRPrefab::Widget::listSelectedIndex(g_bakeShapeList)
                             : kBakeDefaultShapeRow;
    const int index = selected >= 0 && selected < static_cast<int>(std::size(kBakeShapeTypes))
                          ? selected
                          : kBakeDefaultShapeRow;
    const float p1 = g_bakeParam1Slider != IREntity::kNullEntity
                         ? IRPrefab::Widget::sliderValue(g_bakeParam1Slider)
                         : 5.0f;
    const float p2 = g_bakeParam2Slider != IREntity::kNullEntity
                         ? IRPrefab::Widget::sliderValue(g_bakeParam2Slider)
                         : 3.0f;
    return {
        kBakeShapeTypes[index],
        bakeShapeParams(kBakeShapeTypes[index], p1, p2),
        kPaletteColors[g_editor.activeSwatchIdx_],
        IRMath::SDF::SHAPE_FLAG_VISIBLE
    };
}

void addEditorShapePart() {
    if (!g_entitySceneMode) {
        g_entitySceneMode = true;
        clearUndoHistory();
        if (g_editor.editableVoxelSet_ != IREntity::kNullEntity) {
            IREntity::destroyEntity(g_editor.editableVoxelSet_);
        }
        g_entityScene.begin();
    }
    g_entityScene.addShapePart(
        selectedShapeDescription(),
        IRComponents::C_LocalTransform{g_editableSceneOrigin}
    );
    selectEditorPart(g_entityScene.selectedIndex());
}

void pushUndoRecord(UndoRecord record) {
    g_editor.undoTotalBytes_ += record.byteSize();
    g_editor.undoRecords_.push_back(std::move(record));
    while (g_editor.undoTotalBytes_ > kUndoByteBudget && g_editor.undoRecords_.size() > 1) {
        g_editor.undoTotalBytes_ -= g_editor.undoRecords_.front().byteSize();
        g_editor.undoRecords_.pop_front();
    }
}

void removeSelectedPart() {
    if (!g_entitySceneMode || g_entityScene.selectedPart() == nullptr) {
        return;
    }
    std::optional<RemovedEditorPart> removed = g_entityScene.removeSelectedPart();
    if (!removed) {
        return;
    }
    UndoRecord record;
    record.removedPart_ = std::move(removed);
    pushUndoRecord(std::move(record));
    selectEditorPart(g_entityScene.selectedIndex());
}

void commitPartCreation(std::vector<IREntity::EntityId> created) {
    if (created.empty()) {
        return;
    }
    UndoRecord record;
    record.createdParts_ = std::move(created);
    pushUndoRecord(std::move(record));
    selectEditorPart(g_entityScene.selectedIndex());
}

void commitPartClones(EntitySceneCloneResult result) {
    if (!result.error_.empty()) {
        IR_LOG_ERROR("Part array failed: {}", result.error_);
        return;
    }
    commitPartCreation(std::move(result.entities_));
}

void applyRadialArray(int count, float radius, float perCopyYaw) {
    if (!g_entitySceneMode || g_entityScene.selectedEntity() == IREntity::kNullEntity) {
        return;
    }
    constexpr vec3 kAxis(0.0f, 0.0f, 1.0f);
    commitPartClones(g_entityScene.cloneSelected(
        g_moduleHost.script(),
        IRPrefab::Arrays::radial(count, kAxis, radius, perCopyYaw),
        kAxis,
        count
    ));
}

void applyLinearArray(int count, vec3 step) {
    if (!g_entitySceneMode || g_entityScene.selectedEntity() == IREntity::kNullEntity) {
        return;
    }
    commitPartClones(g_entityScene.cloneSelected(
        g_moduleHost.script(),
        IRPrefab::Arrays::linear(count, step),
        vec3(0.0f, 0.0f, 1.0f),
        0
    ));
}

void toggleRotationalSymmetry() {
    if (g_symmetry.rotationalOrder_ > 0) {
        g_symmetry.rotationalOrder_ = 0;
        return;
    }
    const IREntity::EntityId selected = g_entityScene.selectedEntity();
    for (const EditorPart &part : g_entityScene.parts()) {
        if (part.entity_ == selected && part.rotationalOrder_ > 1) {
            g_symmetry.rotationalOrder_ = part.rotationalOrder_;
            g_symmetry.rotationalAxis_ = part.groupAxis_;
            return;
        }
    }
}

// Skeleton tree panel widget entity IDs.
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

// Apply a single placement / erasure edit to one cell, appending the prior
// state to the in-flight stroke buffer. `flat` is the linear pool index so the
// per-voxel mutation reuses the precomputed offset. `boneId` is 0 (identity) for
// normal color-paint; the active bone index in bone-paint mode. Writes
// the raw `voxels_` span directly — callers always finish a batch of these
// (single edit, line, AABB, face-fill, or SDF bake) with one `commitStroke()`,
// which resyncs derived state once for the whole batch. Not called directly:
// applyEdit fans an edit out across the enabled mirror planes into this.
void applyEditRaw(
    IREntity::EntityId voxelSetEntity,
    C_VoxelSetNew &set,
    ivec3 localIdx,
    std::size_t flat,
    bool place,
    Color placeColor,
    std::uint8_t boneId
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

// Scratch buffer for the mirror expansion, retained across edits so a symmetric
// stroke doesn't reallocate per voxel (applyMirrors clears it on entry).
std::vector<ivec3> g_mirrorScratch;

// Apply a placement / erasure at `localIdx`, mirrored across every enabled
// symmetry plane. Every mirror copy lands in the one pending stroke, so a
// single Ctrl+Z restores the whole symmetric edit and the derived-state resync
// still runs once per stroke in commitStroke. With symmetry off this is a thin
// pass-through to applyEditRaw. `flat` is localIdx's precomputed pool index.
void applyEdit(
    IREntity::EntityId voxelSetEntity,
    C_VoxelSetNew &set,
    ivec3 localIdx,
    std::size_t flat,
    bool place,
    Color placeColor,
    std::uint8_t boneId = 0
) {
    if (g_symmetry.enableX_ || g_symmetry.enableY_ || g_symmetry.enableZ_) {
        applyMirrors(localIdx, g_symmetry, g_mirrorScratch);
    } else {
        g_mirrorScratch.clear();
        g_mirrorScratch.push_back(localIdx);
    }

    const EditorPart *sourcePart = nullptr;
    g_rotationGroupScratch.clear();
    g_rotationSetScratch.clear();
    if (g_symmetry.rotationalOrder_ > 1) {
        for (const EditorPart &part : g_entityScene.parts()) {
            if (part.entity_ == voxelSetEntity) {
                sourcePart = &part;
                break;
            }
        }
        if (sourcePart != nullptr && sourcePart->groupId_ != 0) {
            for (const EditorPart &part : g_entityScene.parts()) {
                if (part.groupId_ == sourcePart->groupId_ &&
                    part.kind_ == EditorPartKind::VOXEL_SET) {
                    g_rotationGroupScratch.push_back(&part);
                    g_rotationSetScratch.push_back(
                        &IREntity::getComponent<C_VoxelSetNew>(part.entity_)
                    );
                }
            }
        }
    }

    if (g_rotationGroupScratch.size() != static_cast<std::size_t>(g_symmetry.rotationalOrder_)) {
        g_rotationGroupScratch.clear();
        g_rotationSetScratch.clear();
    }
    std::size_t sourceGroupIndex = 0;
    for (std::size_t i = 0; i < g_rotationGroupScratch.size(); ++i) {
        if (g_rotationGroupScratch[i]->entity_ == voxelSetEntity) {
            sourceGroupIndex = i;
            break;
        }
    }

    for (const ivec3 &mirroredCell : g_mirrorScratch) {
        if (g_rotationGroupScratch.empty()) {
            if (mirroredCell.x < 0 || mirroredCell.x >= set.size_.x || mirroredCell.y < 0 ||
                mirroredCell.y >= set.size_.y || mirroredCell.z < 0 ||
                mirroredCell.z >= set.size_.z) {
                continue;
            }
            const std::size_t mirroredFlat =
                static_cast<std::size_t>(IRMath::index3DtoIndex1D(mirroredCell, set.size_));
            if (mirroredFlat < set.voxels_.size()) {
                applyEditRaw(
                    voxelSetEntity,
                    set,
                    mirroredCell,
                    mirroredFlat,
                    place,
                    placeColor,
                    boneId
                );
            }
            continue;
        }

        for (std::size_t i = 0; i < g_rotationGroupScratch.size(); ++i) {
            C_VoxelSetNew &targetSet = *g_rotationSetScratch[i];
            const int steps = static_cast<int>(i) - static_cast<int>(sourceGroupIndex);
            const ivec3 targetCell = rotateCell(
                mirroredCell,
                targetSet.size_,
                g_symmetry.rotationalAxis_,
                steps,
                g_symmetry.rotationalOrder_
            );
            if (targetCell.x < 0 || targetCell.x >= targetSet.size_.x || targetCell.y < 0 ||
                targetCell.y >= targetSet.size_.y || targetCell.z < 0 ||
                targetCell.z >= targetSet.size_.z) {
                continue;
            }
            const std::size_t targetFlat =
                static_cast<std::size_t>(IRMath::index3DtoIndex1D(targetCell, targetSet.size_));
            if (targetFlat < targetSet.voxels_.size()) {
                applyEditRaw(
                    g_rotationGroupScratch[i]->entity_,
                    targetSet,
                    targetCell,
                    targetFlat,
                    place,
                    placeColor,
                    boneId
                );
            }
        }
    }
}

void commitStroke(bool derivedStateAlreadySynced = false) {
    if (g_editor.pendingStroke_.edits_.empty()) {
        return;
    }
    std::vector<IREntity::EntityId> touchedSets;
    if (!derivedStateAlreadySynced) {
        for (const UndoEdit &edit : g_editor.pendingStroke_.edits_) {
            if (std::find(touchedSets.begin(), touchedSets.end(), edit.voxelSet_) ==
                touchedSets.end()) {
                touchedSets.push_back(edit.voxelSet_);
            }
        }
    }
    pushUndoRecord(std::move(g_editor.pendingStroke_));
    g_editor.pendingStroke_.edits_.clear();
    g_editor.pendingStroke_.edits_.reserve(kUndoStrokeReserve);
    for (IREntity::EntityId entity : touchedSets) {
        IREntity::getComponent<C_VoxelSetNew>(entity).resyncAfterRawEdits();
    }
}

void remapUndoRecordEntity(
    UndoRecord &record, IREntity::EntityId removed, IREntity::EntityId restored
) {
    for (UndoEdit &edit : record.edits_) {
        if (edit.voxelSet_ == removed) {
            edit.voxelSet_ = restored;
        }
    }
    for (IREntity::EntityId &created : record.createdParts_) {
        if (created == removed) {
            created = restored;
        }
    }
}

void remapUndoEntity(IREntity::EntityId removed, IREntity::EntityId restored) {
    for (UndoRecord &record : g_editor.undoRecords_) {
        remapUndoRecordEntity(record, removed, restored);
    }
    for (std::deque<UndoRecord> &records : g_editor.perFrameUndoStacks_) {
        for (UndoRecord &record : records) {
            remapUndoRecordEntity(record, removed, restored);
        }
    }
    remapUndoRecordEntity(g_editor.pendingStroke_, removed, restored);
}

void undoOne() {
    if (g_editor.undoRecords_.empty()) {
        return;
    }
    UndoRecord rec = std::move(g_editor.undoRecords_.back());
    g_editor.undoRecords_.pop_back();
    g_editor.undoTotalBytes_ -= rec.byteSize();
    if (rec.removedPart_) {
        const IREntity::EntityId removed = rec.removedPart_->part_.entity_;
        const EntitySceneRestoreResult restored =
            g_entityScene.restorePart(g_moduleHost.script(), *rec.removedPart_);
        if (!restored.ok()) {
            IR_LOG_ERROR("Part restore failed: {}", restored.error_);
            pushUndoRecord(std::move(rec));
            return;
        }
        remapUndoEntity(removed, restored.entity_);
        selectEditorPart(g_entityScene.selectedIndex());
        return;
    }
    if (!rec.createdParts_.empty()) {
        g_entityScene.removeParts(rec.createdParts_);
        selectEditorPart(g_entityScene.selectedIndex());
    }
    // Replay in reverse so overlapping edits inside a stroke restore
    // in last-write-wins order — same property the forward edit chain
    // produced when authoring.
    std::vector<IREntity::EntityId> touchedSets;
    for (auto it = rec.edits_.rbegin(); it != rec.edits_.rend(); ++it) {
        if (!IREntity::entityExists(it->voxelSet_)) {
            continue;
        }
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
        if (!IREntity::entityExists(id)) {
            continue;
        }
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

// Which cell a left-click gesture edits, given the face it picked.
//
// Erase acts on the hit voxel itself. Place normally lands in FRONT of the hit
// face (`voxelPos_ + faceNormal_`); with Alt held the normal inverts, so the
// voxel lands on the FAR side of the picked face instead. That inversion is the
// only gesture that reaches a cell *below* standing geometry: the picker exposes
// a voxel's -x, -y and -z faces only, so every un-inverted placement grows
// toward smaller x, y or z, and +z is unreachable at any camera yaw
// (docs/design/editor-authoring-friction.md §2g F-2g-1).
//
// `invert` is ignored in erase mode — the hit cell has no far side.
ivec3 editTargetCell(const IRPrefab::Picking::RayHit &hit, bool erase, bool invert) {
    if (erase)
        return hit.voxelPos_;
    return invert ? hit.voxelPos_ - hit.faceNormal_ : hit.voxelPos_ + hit.faceNormal_;
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
    // Both branches write the raw `voxels_` span, so the set's derived state has
    // to be rebuilt for the arriving frame — the same contract every edit path
    // here already honours through commitStroke / undoOne
    // (engine/prefabs/irreden/voxel/CLAUDE.md).
    //
    // The load-bearing half is the pool's active mask. It mirrors
    // `color_.alpha_ != 0`, it is what `c_voxel_visibility_compact` reads
    // *instead of* alpha, and it lives in the pool rather than in the
    // voxel records — so copying records updates alpha and leaves the mask
    // describing the frame that just left. A step then renders a blend of the
    // two poses: cells the departing frame had inactive stay culled however live
    // the arriving frame says they are. Every alpha-reading check still passes,
    // which is why the bird session asserts the mask directly: a wingless
    // frame 1 passes every alpha check and fails only the mask check.
    vs.resyncAfterRawEdits();
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
        "Frame: {} / {}  [{}  {:.0f} FPS]",
        g_anim.activeFrame_ + 1,
        g_anim.frameCount(),
        g_anim.playing_ ? "PLAYING" : "PAUSED",
        g_anim.fps_
    );
}

// --- Skeletal joint authoring + FK posing -------------
//
// One rig per editor session: a rig-root entity carrying C_Skeleton, with
// joint entities parented under it via CHILD_OF. Each joint carries C_Joint
// and the engine's C_LocalTransform; an orange JOINT_MARKER sphere
// (IRPrefab::Gizmo::createJointMarker), a translate gizmo for placement, and
// a rotate gizmo for FK posing — all anchored to the joint itself. The index
// of a joint in C_Skeleton.joints_ IS its bone_id; bindPose_ is kept parallel.
// The "active" joint is the parent for the next add (B); R starts a fresh
// chain off the rig root.
//
// Placement vs posing: a TRANSLATE_ARROW drag on a joint is
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

// "Set current pose as bind": capture every joint's live local
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
    // per-joint rotate rings for FK posing — every drag mutates the
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
// than chaining off the last-added joint.
void resetJointChain() {
    g_jointTool.activeJointIdx_ = -1;
    IR_LOG_INFO("Joint chain reset — next joint parents to the rig root.");
}

// Author a short starter chain so the feature is visible on launch and in
// auto-screenshots, mirroring the perimeter gizmo references in initEntities.
//
// This also rigs the chain with a skinned voxel bar (the FK verification
// vehicle): a 31×3×3 bar on the rig root, painted one bone per third by
// nearest joint. UPDATE_JOINT_MATRICES allocates the skeleton's slot block on
// its first tick and auto-seeds the per-voxel bone→slot indices, so
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
    IREntity::setComponent(rigRoot, C_EditorReference{});
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

// Points the parameter sliders at recipe @p recipeIndex: one per declared
// param, at its range and default; the rest disabled. -1 disables all.
void configureRecipeSliders(int recipeIndex) {
    g_slidersRecipe = recipeIndex;
    const std::vector<ModuleRecipe> &recipes = g_moduleHost.recipes();
    const bool valid = recipeIndex >= 0 && recipeIndex < static_cast<int>(recipes.size());
    for (int i = 0; i < kMaxRecipeParams; ++i) {
        auto &slider = IREntity::getComponent<C_WidgetSlider>(g_recipeSliders[i]);
        const bool used =
            valid &&
            i < static_cast<int>(recipes[static_cast<std::size_t>(recipeIndex)].params_.size());
        if (used) {
            const RecipeParam &param =
                recipes[static_cast<std::size_t>(recipeIndex)].params_[static_cast<std::size_t>(i)];
            slider.label_ = param.name_;
            slider.minValue_ = param.min_;
            slider.maxValue_ = param.max_;
            slider.currentValue_ = param.default_;
        } else {
            slider.label_.clear();
            slider.minValue_ = 0.0f;
            slider.maxValue_ = 1.0f;
            slider.currentValue_ = 0.0f;
        }
        IRPrefab::Widget::setDisabled(g_recipeSliders[i], !used);
    }
}

// Evaluates recipe @p recipeIndex at the slider values and writes its cells
// into the editable set as one stroke, so one Ctrl+Z removes them all. Cells
// outside the set are skipped and counted.
void applyRecipe(int recipeIndex) {
    if (g_sceneVoxelSetEntity == IREntity::kNullEntity)
        return;
    const ModuleRecipe &recipe = g_moduleHost.recipes()[static_cast<std::size_t>(recipeIndex)];
    std::vector<float> values;
    values.reserve(recipe.params_.size());
    for (std::size_t i = 0; i < recipe.params_.size(); ++i)
        values.push_back(IRPrefab::Widget::sliderValue(g_recipeSliders[i]));
    auto &set = IREntity::getComponent<C_VoxelSetNew>(g_sceneVoxelSetEntity);
    std::vector<RecipeCell> cells;
    std::string error;
    if (!g_moduleHost
             .evaluate(static_cast<std::size_t>(recipeIndex), values, set.size_, cells, error)) {
        IR_LOG_ERROR("Recipe apply failed: {}", error);
        return;
    }
    const Color fallbackColor = kPaletteColors[g_editor.activeSwatchIdx_];
    int written = 0;
    int skipped = 0;
    for (const RecipeCell &cell : cells) {
        const ivec3 local = cell.local_;
        if (local.x < 0 || local.x >= set.size_.x || local.y < 0 || local.y >= set.size_.y ||
            local.z < 0 || local.z >= set.size_.z) {
            ++skipped;
            continue;
        }
        const std::size_t flat =
            static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, set.size_));
        applyEditRaw(
            g_sceneVoxelSetEntity,
            set,
            local,
            flat,
            true,
            cell.color_.value_or(fallbackColor),
            0
        );
        ++written;
    }
    commitStroke();
    std::string valueText;
    for (std::size_t i = 0; i < values.size(); ++i)
        valueText +=
            (i == 0 ? "" : ",") + recipe.params_[i].name_ + "=" + std::to_string(values[i]);
    IR_LOG_INFO(
        "recipe_applied name={} values={} cells={} skipped={}",
        recipe.name_,
        valueText,
        written,
        skipped
    );
}

void updateRecipesPanel() {
    if (g_recipesPanel == IREntity::kNullEntity)
        return;
    const int selected = IRPrefab::Widget::listSelectedIndex(g_recipeList);
    if (selected != g_slidersRecipe)
        configureRecipeSliders(selected);
    if (selected >= 0 && IRPrefab::Widget::wasClicked(g_recipeApplyBtn))
        applyRecipe(selected);
}

bool cursorInRect(vec2 cursor, ivec2 pos, ivec2 size) {
    return cursor.x >= static_cast<float>(pos.x) && cursor.y >= static_cast<float>(pos.y) &&
           cursor.x < static_cast<float>(pos.x + size.x) &&
           cursor.y < static_cast<float>(pos.y + size.y);
}

// True while the cursor is over the module UI. A rectangle test rather than
// widget hover: a docked panel holds widgets the module built, and a click on
// any of them must not fall through to the scene.
bool cursorOverModuleUi() {
    const vec2 cursor = IRPrefab::Layout::mousePositionInGuiTrixels();
    if (g_componentsPanel != IREntity::kNullEntity &&
        cursorInRect(cursor, kComponentsPanelPos, kComponentsPanelSize))
        return true;
    if (g_recipesPanel == IREntity::kNullEntity)
        return false;
    if (cursorInRect(cursor, kRecipesPanelPos, kRecipesPanelSize))
        return true;
    for (const DockedModulePanel &docked : g_dockedModulePanels) {
        if (cursorInRect(cursor, docked.pos_, kModulePanelSize))
            return true;
    }
    return false;
}

// What ATTACH targets: the root while ROOT is checked, else the selected part.
// Nullopt outside entity-scene mode or with no part selected.
std::optional<int> componentTarget() {
    if (!g_entitySceneMode || !g_entityScene.active())
        return std::nullopt;
    if (IRPrefab::Widget::checkboxState(g_componentRootToggle))
        return kEntitySceneRootTarget;
    const int selected = g_entityScene.selectedIndex();
    return selected >= 0 ? std::optional<int>{selected} : std::nullopt;
}

void destroyComponentFieldRows() {
    for (const ComponentFieldRow &row : g_componentFieldRows) {
        if (row.label_ != IREntity::kNullEntity)
            IREntity::destroyEntity(row.label_);
        if (row.input_ != IREntity::kNullEntity)
            IREntity::destroyEntity(row.input_);
    }
    g_componentFieldRows.clear();
    for (IREntity::EntityId *pager :
         {&g_componentPagePrevBtn, &g_componentPageNextBtn, &g_componentPageLabel}) {
        if (*pager != IREntity::kNullEntity)
            IREntity::destroyEntity(*pager);
        *pager = IREntity::kNullEntity;
    }
}

// Builds page g_componentFieldPage of @p record's fields.
void buildComponentFieldRows(const ComponentRecord &record) {
    if (record.source_ == ComponentSource::ENGINE) {
        ComponentFieldRow note;
        note.label_ = IRPrefab::Widget::makeLabel(
            ivec2(kComponentFieldLabelX, componentFieldRowY(0)),
            "NO FIELD REFLECTION"
        );
        g_componentFieldRows.push_back(note);
        ComponentFieldRow overrides;
        overrides.input_ = IRPrefab::Widget::makeTextInput(
            ivec2(kComponentFieldLabelX, componentFieldRowY(1)),
            kComponentOverridesInputSize,
            record.overrides_
        );
        g_componentFieldRows.push_back(overrides);
        return;
    }
    const int fieldCount = static_cast<int>(record.fields_.size());
    const int pageCount = componentFieldPageCount(fieldCount);
    const int first = g_componentFieldPage * kComponentFieldRowsPerPage;
    const int last = IRMath::min(first + kComponentFieldRowsPerPage, fieldCount);
    for (int i = first; i < last; ++i) {
        const ComponentField &field = record.fields_[static_cast<std::size_t>(i)];
        const int rowY = componentFieldRowY(i - first);
        const ivec2 inputPos(kComponentFieldInputX, rowY);
        ComponentFieldRow row;
        row.field_ = i;
        row.label_ = IRPrefab::Widget::makeLabel(ivec2(kComponentFieldLabelX, rowY), field.name_);
        switch (field.type_) {
        case IRScript::LuaFieldType::BOOL:
            row.checkbox_ = true;
            row.input_ = IRPrefab::Widget::makeCheckbox(
                inputPos,
                ivec2(kComponentFieldInputSize.y),
                "",
                std::holds_alternative<bool>(field.value_) && std::get<bool>(field.value_)
            );
            break;
        case IRScript::LuaFieldType::FUNCTION:
        case IRScript::LuaFieldType::TABLE:
            g_componentFieldRows.push_back(row);
            row = ComponentFieldRow{};
            row.label_ = IRPrefab::Widget::makeLabel(
                inputPos,
                std::string("(") + IRScript::toString(field.type_) + ")"
            );
            break;
        default:
            row.input_ = IRPrefab::Widget::makeTextInput(
                inputPos,
                kComponentFieldInputSize,
                formatFieldValue(field.value_)
            );
            break;
        }
        g_componentFieldRows.push_back(row);
    }
    if (pageCount > 1) {
        g_componentPagePrevBtn =
            IRPrefab::Widget::makeButton(kComponentPagePrevPos, kComponentButtonSize, "PREV");
        g_componentPageNextBtn =
            IRPrefab::Widget::makeButton(kComponentPageNextPos, kComponentButtonSize, "NEXT");
        g_componentPageLabel = IRPrefab::Widget::makeLabel(
            kComponentPageLabelPos,
            std::to_string(g_componentFieldPage + 1) + "/" + std::to_string(pageCount)
        );
        IRPrefab::Widget::setDisabled(g_componentPagePrevBtn, g_componentFieldPage == 0);
        IRPrefab::Widget::setDisabled(
            g_componentPageNextBtn,
            g_componentFieldPage == pageCount - 1
        );
    }
}

// Applies what the author typed into @p row. Text the field's type cannot
// hold, or an apply the factory rejects, leaves the record as it was; the
// next sync puts its value back in the input.
void commitComponentFieldText(
    const ComponentFieldRow &row, ComponentRecord &record, IREntity::EntityId entity
) {
    const std::string text = IRPrefab::Widget::textInputValue(row.input_);
    if (text.empty())
        return;
    IRScript::LuaScript &script = g_moduleHost.script();
    std::optional<std::string> error;
    if (row.field_ < 0) {
        error = applyComponentLiteral(script, entity, record, text);
    } else {
        ComponentRecord edited = record;
        ComponentField &field = edited.fields_[static_cast<std::size_t>(row.field_)];
        const std::optional<ComponentFieldValue> parsed = parseFieldText(field.type_, text);
        if (!parsed) {
            error = "'" + text + "' is not a " + IRScript::toString(field.type_);
        } else {
            field.value_ = *parsed;
            error = applyComponentRecord(script, entity, edited);
            if (!error)
                record = std::move(edited);
        }
    }
    if (error) {
        IR_LOG_WARN("Component field not set: {}", *error);
        return;
    }
    IR_LOG_INFO("component_field_set name={} value={}", record.name_, componentLiteral(record));
}

// Keeps each field widget showing its record value, except a text input
// being typed into: focusing one clears it for the new value, and losing
// focus (Enter, or a click elsewhere) commits it.
void syncComponentFieldRows(ComponentRecord &record, IREntity::EntityId entity) {
    for (ComponentFieldRow &row : g_componentFieldRows) {
        if (row.input_ == IREntity::kNullEntity)
            continue;
        if (row.checkbox_) {
            if (IRPrefab::Widget::wasClicked(row.input_)) {
                ComponentRecord edited = record;
                edited.fields_[static_cast<std::size_t>(row.field_)].value_ =
                    IRPrefab::Widget::checkboxState(row.input_);
                if (auto error = applyComponentRecord(g_moduleHost.script(), entity, edited))
                    IR_LOG_WARN("Component field not set: {}", *error);
                else
                    record = std::move(edited);
            }
            const ComponentFieldValue &value =
                record.fields_[static_cast<std::size_t>(row.field_)].value_;
            IRPrefab::Widget::setCheckboxState(
                row.input_,
                std::holds_alternative<bool>(value) && std::get<bool>(value)
            );
            continue;
        }
        const bool focused = IREntity::getComponent<C_WidgetState>(row.input_).focused_;
        if (focused && !row.wasFocused_)
            IRPrefab::Widget::setTextInputValue(row.input_, "");
        else if (!focused && row.wasFocused_)
            commitComponentFieldText(row, record, entity);
        row.wasFocused_ = focused;
        if (focused)
            continue;
        const std::string shown =
            row.field_ < 0
                ? record.overrides_
                : formatFieldValue(record.fields_[static_cast<std::size_t>(row.field_)].value_);
        if (IRPrefab::Widget::textInputValue(row.input_) != shown)
            IRPrefab::Widget::setTextInputValue(row.input_, shown);
    }
}

void updateComponentsPanel() {
    if (g_componentsPanel == IREntity::kNullEntity)
        return;
    const int row = IRPrefab::Widget::listSelectedIndex(g_componentList);
    const std::string *name = row >= 0 && row < static_cast<int>(g_componentNames.size())
                                  ? &g_componentNames[static_cast<std::size_t>(row)]
                                  : nullptr;
    const std::optional<int> target = componentTarget();
    const IREntity::EntityId entity =
        target ? g_entityScene.targetEntity(*target) : IREntity::kNullEntity;
    std::vector<ComponentRecord> *records =
        target ? g_entityScene.targetComponents(*target) : nullptr;
    auto findRecord = [&]() -> ComponentRecord * {
        if (records == nullptr || name == nullptr)
            return nullptr;
        for (ComponentRecord &record : *records) {
            if (record.name_ == *name)
                return &record;
        }
        return nullptr;
    };
    ComponentRecord *record = findRecord();
    IRScript::LuaScript &script = g_moduleHost.script();

    if (IRPrefab::Widget::wasClicked(g_componentAttachBtn) && name != nullptr &&
        records != nullptr && record == nullptr) {
        ComponentRecord attached = makeComponentRecord(script, *name);
        if (auto error = applyComponentRecord(script, entity, attached)) {
            IR_LOG_WARN("Component not attached: {}", *error);
        } else {
            IR_LOG_INFO("component_attached name={} target={}", *name, *target);
            records->push_back(std::move(attached));
            record = &records->back();
        }
    }
    if (IRPrefab::Widget::wasClicked(g_componentDetachBtn) && record != nullptr) {
        detachComponentRecord(script, entity, *record);
        IR_LOG_INFO("component_detached name={} target={}", record->name_, *target);
        records->erase(records->begin() + (record - records->data()));
        record = nullptr;
    }

    std::string status = "PICK A COMPONENT";
    if (!target) {
        status = "NO TARGET (CTRL+P)";
    } else if (name != nullptr) {
        status = (*target == kEntitySceneRootTarget
                      ? std::string("ROOT")
                      : g_entityScene.parts()[static_cast<std::size_t>(*target)].id_) +
                 (record != nullptr ? " ATTACHED" : " NOT ATTACHED");
    }
    IRPrefab::Widget::setLabelText(g_componentStatusLabel, std::move(status));

    const std::string key =
        record != nullptr ? record->name_ + "@" + std::to_string(entity) : std::string{};
    if (key != g_componentFieldRowsKey) {
        destroyComponentFieldRows();
        g_componentFieldPage = 0;
        if (record != nullptr)
            buildComponentFieldRows(*record);
        g_componentFieldRowsKey = key;
    }
    if (record == nullptr)
        return;
    // Synced before a page turn: pressing the pager took the focus from a
    // text input, and its typed text commits here while its row still exists.
    syncComponentFieldRows(*record, entity);
    if (g_componentPageNextBtn == IREntity::kNullEntity)
        return;
    const int lastPage = componentFieldPageCount(static_cast<int>(record->fields_.size())) - 1;
    const int turned = IRMath::clamp(
        g_componentFieldPage + (IRPrefab::Widget::wasClicked(g_componentPageNextBtn) ? 1 : 0) -
            (IRPrefab::Widget::wasClicked(g_componentPagePrevBtn) ? 1 : 0),
        0,
        lastPage
    );
    if (turned != g_componentFieldPage) {
        destroyComponentFieldRows();
        g_componentFieldPage = turned;
        buildComponentFieldRows(*record);
    }
}

} // namespace

// The COMPONENTS panel's list: the module's components in registration order,
// then every other component with a prefab factory, by name.
std::vector<std::string> componentPaletteNames() {
    std::vector<std::string> names;
    for (const IRScript::LuaTypedComponentInfo &info : g_moduleHost.script().luaTypedComponents())
        names.push_back(info.name_);
    const std::size_t moduleCount = names.size();
    for (std::string &factory : IRPrefab::Prefab::listComponentFactories()) {
        if (std::find(
                names.begin(),
                names.begin() + static_cast<std::ptrdiff_t>(moduleCount),
                factory
            ) == names.begin() + static_cast<std::ptrdiff_t>(moduleCount))
            names.push_back(std::move(factory));
    }
    return names;
}

// Builds the COMPONENTS panel when there is anything to attach.
void initComponentsUi() {
    g_componentNames = componentPaletteNames();
    if (g_componentNames.empty())
        return;
    const std::size_t moduleCount = g_moduleHost.script().luaTypedComponents().size();
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < g_componentNames.size(); ++i)
        rows.push_back(i < moduleCount ? g_componentNames[i] : "C++ " + g_componentNames[i]);

    g_componentsPanel =
        IRPrefab::Widget::makePanel(kComponentsPanelPos, kComponentsPanelSize, "COMPONENTS");
    IREntity::setComponent(g_componentsPanel, IRComponents::C_HitBox2DGui{kComponentsPanelSize});
    IREntity::getComponent<IRComponents::C_Widget>(g_componentsPanel).zOrder_ = -1;
    g_componentList = IRPrefab::Widget::makeList(
        kComponentListPos,
        kComponentListSize,
        std::move(rows),
        0,
        kComponentListItemHeight
    );
    g_componentRootToggle = IRPrefab::Widget::makeCheckbox(
        kComponentRootTogglePos,
        kComponentRootToggleSize,
        "ROOT",
        false
    );
    g_componentAttachBtn =
        IRPrefab::Widget::makeButton(kComponentAttachPos, kComponentButtonSize, "ATTACH");
    g_componentDetachBtn =
        IRPrefab::Widget::makeButton(kComponentDetachPos, kComponentButtonSize, "DETACH");
    g_componentStatusLabel = IRPrefab::Widget::makeLabel(kComponentStatusPos, "");
    g_helpEntries.push_back(
        {g_componentsPanel, "COMPONENTS: attach to the selected part, or the root with ROOT."}
    );
    g_helpEntries.push_back(
        {g_componentList, "COMPONENT: module components first, then C++ ones with a factory."}
    );
    g_helpEntries.push_back(
        {g_componentAttachBtn, "ATTACH: add the component; its fields appear below."}
    );
    g_helpEntries.push_back({g_componentDetachBtn, "DETACH: remove the component."});
    g_helpEntries.push_back(
        {g_componentRootToggle, "ROOT: target the entity root instead of the selected part."}
    );
}

// Builds the RECIPES panel and docks one panel per IREditor.registerPanel
// below it. A no-op without --module; false when a panel's build function
// raises, which fails the launch the same way a module load error does.
bool initModuleUi() {
    if (!g_moduleHost.loaded())
        return true;
    g_recipesPanel = IRPrefab::Widget::makePanel(kRecipesPanelPos, kRecipesPanelSize, "RECIPES");
    IREntity::setComponent(g_recipesPanel, IRComponents::C_HitBox2DGui{kRecipesPanelSize});
    IREntity::getComponent<IRComponents::C_Widget>(g_recipesPanel).zOrder_ = -1;
    std::vector<std::string> names;
    for (const ModuleRecipe &recipe : g_moduleHost.recipes())
        names.push_back(recipe.name_);
    const int initialRecipe = names.empty() ? -1 : 0;
    g_recipeList = IRPrefab::Widget::makeList(
        kRecipeListPos,
        kRecipeListSize,
        std::move(names),
        initialRecipe,
        kRecipeListItemHeight
    );
    for (int i = 0; i < kMaxRecipeParams; ++i) {
        const SliderGeometry geom = recipeParamSliderGeometry(i);
        g_recipeSliders[i] =
            IRPrefab::Widget::makeSlider(geom.pos_, geom.size_, "", 0.0f, 1.0f, 0.0f);
    }
    g_recipeApplyBtn = IRPrefab::Widget::makeButton(kRecipeApplyPos, kRecipeApplySize, "APPLY");
    configureRecipeSliders(initialRecipe);
    g_helpEntries.push_back(
        {g_recipesPanel, "RECIPES: pick a module recipe, set its params, APPLY."}
    );
    g_helpEntries.push_back({g_recipeList, "RECIPE: choose the module recipe to apply."});
    g_helpEntries.push_back(
        {g_recipeApplyBtn, "APPLY: write the recipe's cells into the set (Ctrl+Z undoes)."}
    );

    const std::vector<std::string> &panelNames = g_moduleHost.panelNames();
    for (std::size_t i = 0; i < panelNames.size(); ++i) {
        const ivec2 pos = modulePanelPos(static_cast<int>(i));
        const IREntity::EntityId panel =
            IRPrefab::Widget::makePanel(pos, kModulePanelSize, panelNames[i]);
        IREntity::setComponent(panel, IRComponents::C_HitBox2DGui{kModulePanelSize});
        IREntity::getComponent<IRComponents::C_Widget>(panel).zOrder_ = -1;
        g_dockedModulePanels.push_back(DockedModulePanel{panel, pos});
        std::string error;
        if (!g_moduleHost.buildPanel(i, pos, kModulePanelSize, error)) {
            IR_LOG_ERROR("Module panel build failed: {}", error);
            return false;
        }
    }
    return true;
}

namespace Session {

// How far, in subdivided depth units, a picked face may sit from the drawn one
// and still be the same voxel's. The next voxel along any axis is a whole
// subdivision (>= 1) away.
constexpr float kPickDepthAgreement = 0.75f;

// The whole-frame form of the picking contract: every main-canvas texel that
// shows the editable set must pick the face it shows, at the depth it shows it.
// The canvas's own distance readback is the oracle — it records which face slot
// won each texel — so this fails on a pick that is self-consistent with the
// session's aims yet offset from what is actually drawn. Cardinal camera only:
// a slot names its axis directly there.
bool evaluatePickMatchesRender(const void *, std::string &actual) {
    if (g_sceneVoxelSetEntity == IREntity::kNullEntity) {
        actual = "no-editable-set";
        return false;
    }
    const auto &canvas =
        IREntity::getComponent<C_TriangleCanvasTextures>(IRRender::getActiveCanvasEntity());
    std::vector<IRMath::uvec2> ids;
    std::vector<int> distances;
    canvas.readEntityIdCarriers(ids);
    canvas.readDistances(distances);

    const auto &set = IREntity::getComponent<C_VoxelSetNew>(g_sceneVoxelSetEntity);
    const IRMath::vec3 origin = set.globalPositions_[0].pos_;
    const float subdivisions = static_cast<float>(IRRender::getVoxelRenderEffectiveSubdivisions());
    // A drawn face cell is stamped with the depth of the corner-anchored
    // micro-voxel it bounds, which sits this far behind the point the ray
    // enters the face at, in subdivided depth units.
    const float drawnDepthLead = 1.5f * subdivisions - 1.0f;
    int shown = 0;
    int agree = 0;
    float worstDepthError = 0.0f;
    for (int y = 0; y < canvas.size_.y; ++y) {
        for (int x = 0; x < canvas.size_.x; ++x) {
            const std::size_t texel = static_cast<std::size_t>(y) * canvas.size_.x + x;
            if (static_cast<IREntity::EntityId>(IRRender::decodeCarrierEntityId(ids[texel])) !=
                g_sceneVoxelSetEntity) {
                continue;
            }
            ++shown;
            const IRRender::DecodedCompositeDepth drawn =
                IRRender::decodeCompositeDepth(static_cast<float>(distances[texel]));
            const std::optional<IRPrefab::Picking::GridRayHit> hit = IRPrefab::Picking::castGridRay(
                IRRender::mainCanvasTexelWorldPos3DAtIsoDepth(IRMath::ivec2(x, y), 0.0f) - origin,
                kSessionRayDirection,
                set.size_,
                [&set](IRMath::ivec3 local) {
                    return set.voxels_[IRMath::index3DtoIndex1D(local, set.size_)].color_.alpha_ !=
                           0;
                }
            );
            if (!hit || hit->faceNormal_[drawn.face_ % 3] == 0)
                continue;
            const float depthError = IRMath::abs(
                hit->rayT_ * 3.0f * subdivisions + drawnDepthLead - static_cast<float>(drawn.iso_)
            );
            worstDepthError = IRMath::max(worstDepthError, depthError);
            if (depthError < kPickDepthAgreement)
                ++agree;
        }
    }
    actual = "texels=" + std::to_string(shown) + " agree=" + std::to_string(agree) +
             " worstDepthError=" + std::to_string(worstDepthError);
    return shown > 0 && agree == shown;
}

// Reads one recipe pick expectation through the editor's own edit pick, so a
// parked cursor is judged by the ray a click from that spot would cast.
bool evaluatePickCheck(const void *context, std::string &actual) {
    const PickCheck &check = *static_cast<const PickCheck *>(context);
    const std::optional<IRPrefab::Picking::RayHit> hit = pickEditable();
    if (!hit) {
        actual = "no-hit";
        return false;
    }
    actual = "voxel=(" + std::to_string(hit->voxelPos_.x) + "," + std::to_string(hit->voxelPos_.y) +
             "," + std::to_string(hit->voxelPos_.z) + ") normal=(" +
             std::to_string(hit->faceNormal_.x) + "," + std::to_string(hit->faceNormal_.y) + "," +
             std::to_string(hit->faceNormal_.z) + ")";
    return hit->voxelPos_ == check.worldVoxel_;
}

// Reads one recipe occupancy expectation against the live editable set at a
// segment's capture frame. This is the check that makes a
// session positive-fire: a gesture that was swallowed — click intercepted by a
// widget or a reference shape, aim occluded, drag never committing — leaves the
// cell in its old state and FAILs here, instead of quietly authoring nothing.
bool evaluateOccupancyCheck(const void *context, std::string &actual) {
    const OccupancyCheck &check = *static_cast<const OccupancyCheck *>(context);
    const IRMath::ivec3 cell = check.localCell_;
    const std::string where =
        (check.partIndex_ >= 0 ? "part=" + std::to_string(check.partIndex_) + " " : "") + "cell=(" +
        std::to_string(cell.x) + "," + std::to_string(cell.y) + "," + std::to_string(cell.z) + ")";
    IREntity::EntityId voxelSetEntity = g_sceneVoxelSetEntity;
    if (check.partIndex_ >= 0 &&
        check.partIndex_ < static_cast<int>(g_entityScene.parts().size())) {
        voxelSetEntity = g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)].entity_;
    }
    if (voxelSetEntity == IREntity::kNullEntity) {
        actual = where + " no-editable-set";
        return false;
    }
    const auto &set = IREntity::getComponent<C_VoxelSetNew>(voxelSetEntity);
    if (cell.x < 0 || cell.x >= set.size_.x || cell.y < 0 || cell.y >= set.size_.y || cell.z < 0 ||
        cell.z >= set.size_.z) {
        actual = where + " out-of-bounds";
        return false;
    }
    const std::size_t flat = static_cast<std::size_t>(IRMath::index3DtoIndex1D(cell, set.size_));
    if (flat >= set.voxels_.size()) {
        actual = where + " unallocated";
        return false;
    }
    if (check.source_ == Session::CheckSource::POOL_ACTIVE_MASK) {
        // The pool-side mirror of alpha, and what the compact shader actually
        // reads. Read through the same slot arithmetic the pool uses.
        auto poolOpt = IREntity::getComponentOptional<IRComponents::C_VoxelPool>(set.canvasEntity_);
        if (!poolOpt.has_value()) {
            actual = where + " no-pool";
            return false;
        }
        const std::vector<std::uint32_t> &mask = poolOpt.value()->getActiveMask();
        const std::size_t slot = set.voxelStartIdx_ + flat;
        const std::size_t word = slot / IRComponents::kVoxelActiveMaskBits;
        if (word >= mask.size()) {
            actual = where + " slot-out-of-mask";
            return false;
        }
        const bool active = (mask[word] >> (slot % IRComponents::kVoxelActiveMaskBits) & 1u) != 0u;
        actual = where + " poolActive=" + (active ? "yes" : "no") +
                 " want=" + (check.expectOccupied_ ? "yes" : "no");
        return active == check.expectOccupied_;
    }
    // Active = non-zero alpha, the same liveness test the picker and the GPU
    // pipeline use (C_Voxel::activate / deactivate).
    const Color color = set.voxels_[flat].color_;
    const bool occupied = color.alpha_ != 0;
    actual = where + " occupied=" + (occupied ? "yes" : "no") +
             " want=" + (check.expectOccupied_ ? "yes" : "no");
    if (!check.expectColor_)
        return occupied == check.expectOccupied_;
    // Palette check (expectVoxelColor): RGB only — alpha is the occupancy
    // channel, already covered above.
    const Color want = *check.expectColor_;
    const auto rgb = [](Color c) {
        return "rgb(" + std::to_string(c.red_) + "," + std::to_string(c.green_) + "," +
               std::to_string(c.blue_) + ")";
    };
    actual += " color=" + rgb(color) + " wantColor=" + rgb(want);
    return occupied && color.red_ == want.red_ && color.green_ == want.green_ &&
           color.blue_ == want.blue_;
}

bool evaluatePartTransformCheck(const void *context, std::string &actual) {
    const PartTransformCheck &check = *static_cast<const PartTransformCheck *>(context);
    if (check.partIndex_ < 0 ||
        check.partIndex_ >= static_cast<int>(g_entityScene.parts().size())) {
        actual = "part index out of range";
        return false;
    }
    const IREntity::EntityId entity =
        g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)].entity_;
    const vec3 translation = IREntity::getComponent<C_LocalTransform>(entity).translation_;
    const vec3 delta = translation - check.expected_;
    const bool equal = IRMath::abs(delta.x) <= check.tolerance_ &&
                       IRMath::abs(delta.y) <= check.tolerance_ &&
                       IRMath::abs(delta.z) <= check.tolerance_;
    actual = "translation=(" + std::to_string(translation.x) + "," + std::to_string(translation.y) +
             "," + std::to_string(translation.z) + ")";
    return equal == check.expectEqual_;
}

bool evaluatePartCountCheck(const void *context, std::string &actual) {
    const PartCountCheck &check = *static_cast<const PartCountCheck *>(context);
    const int count = static_cast<int>(g_entityScene.parts().size());
    actual = "parts=" + std::to_string(count) + " want=" + std::to_string(check.expected_);
    return count == check.expected_;
}

bool evaluatePartAuthoredCountCheck(const void *context, std::string &actual) {
    const PartAuthoredCountCheck &check = *static_cast<const PartAuthoredCountCheck *>(context);
    if (check.partIndex_ < 0 ||
        check.partIndex_ >= static_cast<int>(g_entityScene.parts().size())) {
        actual = "part index out of range";
        return false;
    }
    const IREntity::EntityId entity =
        g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)].entity_;
    const auto &set = IREntity::getComponent<C_VoxelSetNew>(entity);
    int count = 0;
    for (const C_Voxel &voxel : set.authoredRecords()) {
        if (voxel.color_.alpha_ != 0) {
            ++count;
        }
    }
    actual = "authored=" + std::to_string(count) + " want=" + std::to_string(check.expected_);
    return count == check.expected_;
}

bool evaluatePartEditableCheck(const void *context, std::string &actual) {
    const PartEditableCheck &check = *static_cast<const PartEditableCheck *>(context);
    if (check.partIndex_ < 0 ||
        check.partIndex_ >= static_cast<int>(g_entityScene.parts().size())) {
        actual = "part index out of range";
        return false;
    }
    const IREntity::EntityId entity =
        g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)].entity_;
    const bool editable = isEditable(entity);
    actual = "editable=" + std::string(editable ? "yes" : "no") +
             " want=" + (check.expected_ ? "yes" : "no");
    return editable == check.expected_;
}

bool evaluateCanvasCountCheck(const void *context, std::string &actual) {
    const CanvasCountCheck &check = *static_cast<const CanvasCountCheck *>(context);
    const int count = IRPrefab::EntityCanvas::count();
    const int expected = g_modePreviewBaseCanvasCount + check.expectedOffset_;
    actual = "canvases=" + std::to_string(count) + " want=" + std::to_string(expected);
    return count == expected;
}

bool evaluateRotationModeCheck(const void *context, std::string &actual) {
    const RotationModeCheck &check = *static_cast<const RotationModeCheck *>(context);
    if (check.partIndex_ < 0 ||
        check.partIndex_ >= static_cast<int>(g_entityScene.parts().size())) {
        actual = "part index out of range";
        return false;
    }
    const EditorPart &part = g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)];
    const IREntity::EntityId entity = part.entity_;
    const RotationMode mode = IREntity::getComponent<C_RotationMode>(entity).mode_;
    bool canvasSizeMatches = part.canvasSize_ == IRMath::ivec2{0};
    if (IRPrefab::RotationMode::ownsEntityCanvas(mode)) {
        const auto canvas = IREntity::getComponentOptional<C_EntityCanvas>(entity);
        canvasSizeMatches = canvas && part.canvasSize_.x > 0 && part.canvasSize_.y > 0 &&
                            part.canvasSize_ == canvas.value()->canvasSize_;
    }
    actual = "mode=" + std::to_string(rotationModeIndex(mode)) +
             " want=" + std::to_string(rotationModeIndex(check.expected_)) +
             " stored_canvas=" + std::to_string(part.canvasSize_.x) + "x" +
             std::to_string(part.canvasSize_.y);
    return mode == check.expected_ && canvasSizeMatches;
}

bool evaluateEditorInputCheck(const void *context, std::string &actual) {
    const EditorInputCheck &check = *static_cast<const EditorInputCheck *>(context);
    int focusedTextInputs = 0;
    int tabCandidates = 0;
    IREntity::forEachComponent<C_HitBox2DGui>([&](IREntity::EntityId &id, C_HitBox2DGui &) {
        const auto &widget = IREntity::getComponent<C_Widget>(id);
        const auto &state = IREntity::getComponent<C_WidgetState>(id);
        if (widget.kind_ == WidgetKind::TEXT_INPUT && state.focused_) {
            ++focusedTextInputs;
        }
        if (IRPrefab::Widget::isTabFocusCandidate(widget)) {
            ++tabCandidates;
        }
    });

    const int selectedPart = g_entityScene.selectedIndex();
    if (check.kind_ == EditorInputCheckKind::SELECTED_PART_NO_TEXT_FOCUS ||
        check.kind_ == EditorInputCheckKind::TEXT_FOCUS_RELEASED) {
        actual = "selectedPart=" + std::to_string(selectedPart) +
                 " want=" + std::to_string(check.expectedPart_) +
                 " focusedTextInputs=" + std::to_string(focusedTextInputs);
        return selectedPart == check.expectedPart_ && focusedTextInputs == 0;
    }
    if (check.kind_ == EditorInputCheckKind::TEXT_CAPTURED_X) {
        const std::string &text = IRPrefab::Widget::textInputValue(g_jointRenameInput);
        actual = "text=\"" + text + "\" symmetryX=" + (g_symmetry.enableX_ ? "on" : "off") +
                 " focusedTextInputs=" + std::to_string(focusedTextInputs);
        return text.find('x') != std::string::npos && !g_symmetry.enableX_ &&
               focusedTextInputs == 1;
    }
    if (check.kind_ == EditorInputCheckKind::X_SYMMETRY_ENABLED) {
        actual = "symmetryX=" + std::string(g_symmetry.enableX_ ? "on" : "off") +
                 " selectedPart=" + std::to_string(selectedPart);
        return g_symmetry.enableX_ && selectedPart == check.expectedPart_;
    }

    actual = "walkPresses=" + std::to_string(kTabWalkPresses) +
             " tabCandidates=" + std::to_string(tabCandidates);
    return kTabWalkPresses > tabCandidates;
}

// Reads one SliderCheck against the live ANIM panel widget it names — the
// positive fire for dragGuiSlider: a drag that missed the track never
// presses the widget, so its value stays put and this fails instead of
// quietly passing.
bool evaluateSliderCheck(const void *context, std::string &actual) {
    const SliderCheck &check = *static_cast<const SliderCheck *>(context);
    IREntity::EntityId widget = IREntity::kNullEntity;
    switch (check.target_) {
    case SliderTarget::FPS:
        widget = g_fpsSlider;
        break;
    case SliderTarget::SCRUBBER:
        widget = g_scrubberSlider;
        break;
    case SliderTarget::LOD_FINE:
        widget = g_lodFineSlider;
        break;
    case SliderTarget::LOD_COARSE:
        widget = g_lodCoarseSlider;
        break;
    case SliderTarget::LOD_TIER:
        widget = g_lodTierSlider;
        break;
    }
    if (widget == IREntity::kNullEntity) {
        actual = "widget not built";
        return false;
    }
    const float value = IRPrefab::Widget::sliderValue(widget);
    actual = "value=" + std::to_string(value);
    return IRMath::abs(value - check.expected_) <= check.tolerance_;
}

bool evaluatePartGateCheck(const void *context, std::string &actual) {
    const PartGateCheck &check = *static_cast<const PartGateCheck *>(context);
    IREntity::EntityId entity = g_entityScene.previewEntity();
    if (check.partIndex_ >= 0 &&
        check.partIndex_ < static_cast<int>(g_entityScene.parts().size())) {
        entity = g_entityScene.parts()[static_cast<std::size_t>(check.partIndex_)].entity_;
    } else if (check.partIndex_ != -1) {
        actual = "part index out of range";
        return false;
    }
    if (entity == IREntity::kNullEntity || !IREntity::entityExists(entity)) {
        actual = "target entity does not exist";
        return false;
    }
    const auto set = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
    if (!set) {
        actual = "part has no voxel set";
        return false;
    }
    const bool gated = (*set)->lodCulled_;
    const auto pin = IREntity::getComponentOptional<C_LodTierOverride>(entity);
    const int pinnedTier = pin ? static_cast<int>((*pin)->tier_) : -1;
    actual = "band=[" + std::to_string(static_cast<int>((*set)->lodMax_)) + "," +
             std::to_string(static_cast<int>((*set)->lodMin_)) +
             "] gated=" + (gated ? "yes" : "no") + " pinned=" + std::to_string(pinnedTier) +
             " active=" + std::to_string(static_cast<int>(IRRender::getActiveLodLevel()));
    return gated == check.expectGated_ && pinnedTier == check.expectPinnedTier_;
}

bool evaluateManifestCheck(const void *context, std::string &actual) {
    const ManifestCheck &check = *static_cast<const ManifestCheck *>(context);
    const std::filesystem::path path =
        IRUtility::joinPath(std::string(kSceneSaveDir), std::string(kSceneBaseName), ".prefab.lua");
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, error);
    if (error) {
        actual = "no manifest at " + path.string();
        return false;
    }
    if (written < g_runStartFileTime) {
        actual = "manifest predates this run";
        return false;
    }
    std::ifstream in(path);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const bool found = text.find(check.text_) != std::string::npos;
    actual = std::string(found ? "found" : "missing") + " in " + path.string();
    return found == check.expectContains_;
}

bool evaluateComponentCheck(const void *context, std::string &actual) {
    const ComponentCheck &check = *static_cast<const ComponentCheck *>(context);
    for (const IRScript::LuaTypedComponentInfo &info : g_moduleHost.script().luaTypedComponents()) {
        if (info.name_ != check.componentName_)
            continue;
        actual = "component=" + info.name_ + " fields=" + std::to_string(info.fields_.size()) +
                 " want=" + std::to_string(check.fieldCount_);
        return static_cast<int>(info.fields_.size()) == check.fieldCount_;
    }
    actual = "component=" + check.componentName_ + " not enumerated";
    return false;
}

// Counts the label widgets positioned inside the docked panel's rectangle —
// the panel's build function places them, so there is no other handle to
// them.
bool evaluatePanelLabelCheck(const void *context, std::string &actual) {
    const PanelLabelCheck &check = *static_cast<const PanelLabelCheck *>(context);
    const std::vector<std::string> &names = g_moduleHost.panelNames();
    const auto it = std::find(names.begin(), names.end(), check.panelName_);
    const std::size_t index = static_cast<std::size_t>(it - names.begin());
    if (it == names.end() || index >= g_dockedModulePanels.size()) {
        actual = "panel=" + check.panelName_ + " not docked";
        return false;
    }
    const ivec2 lo = g_dockedModulePanels[index].pos_;
    const ivec2 hi = lo + kModulePanelSize;
    int labels = 0;
    std::string text;
    IREntity::forEachComponent<C_WidgetLabel>([&](IREntity::EntityId &id, C_WidgetLabel &label) {
        const ivec2 pos = IREntity::getComponent<C_GuiPosition>(id).pos_;
        if (pos.x < lo.x || pos.y < lo.y || pos.x >= hi.x || pos.y >= hi.y)
            return;
        ++labels;
        text = label.text_;
    });
    actual = "labels=" + std::to_string(labels) + " text=\"" + text + "\"";
    return labels == 1 && text == check.label_;
}

// Reads the component's field straight from the target's live entity row, so
// a reloaded scene is checked against what the manifest spawned, not against
// the panel's record of it.
bool evaluateComponentValueCheck(const void *context, std::string &actual) {
    const ComponentValueCheck &check = *static_cast<const ComponentValueCheck *>(context);
    const std::string where =
        "target=" + std::to_string(check.target_) + " " + check.component_ + "." + check.field_;
    const IREntity::EntityId entity = g_entityScene.targetEntity(check.target_);
    if (entity == IREntity::kNullEntity) {
        actual = where + " no-target";
        return false;
    }
    IRScript::LuaScript &script = g_moduleHost.script();
    const IRScript::LuaTypedComponentInfo *info =
        IRVoxelEditor::detail::findModuleComponent(script, check.component_);
    const sol::object row =
        info ? script.readLuaTypedComponent(entity, info->componentId_) : sol::object{};
    if (row.get_type() != sol::type::table) {
        actual = where + " absent";
        return !check.expected_.has_value();
    }
    if (!check.expected_) {
        actual = where + " attached";
        return false;
    }
    const auto field =
        std::find_if(info->fields_.begin(), info->fields_.end(), [&check](const auto &f) {
            return f.name_ == check.field_;
        });
    if (field == info->fields_.end()) {
        actual = where + " no-such-field";
        return false;
    }
    const ComponentFieldValue value =
        IRVoxelEditor::detail::fieldValueFromRow(field->type_, row.as<sol::table>()[check.field_]);
    actual = where + " value=" + formatFieldValue(value) +
             (check.expectEqual_ ? " want=" : " want!=") + formatFieldValue(*check.expected_);
    return (value == *check.expected_) == check.expectEqual_;
}

} // namespace Session

// Resolves a component session from <module dir>/session_expect.lua's
// `<key> = { component, field, value, default }` against the loaded module and
// the COMPONENTS panel layout: `componentAttach` for component_attach,
// `componentFieldPage` for component_field_page, whose field must lie past the
// first page of the field area, and `componentFieldKey` for
// component_field_key, whose field name must not be a Lua identifier.
Session::ComponentAttachSpec resolveComponentAttachSpec(Session::Id id) {
    const bool paged = id == Session::Id::COMPONENT_FIELD_PAGE;
    const bool keyed = id == Session::Id::COMPONENT_FIELD_KEY;
    const std::string entryKey = paged   ? "componentFieldPage"
                                 : keyed ? "componentFieldKey"
                                         : "componentAttach";
    Session::ComponentAttachSpec spec;
    spec.session_ = paged   ? "component_field_page"
                    : keyed ? "component_field_key"
                            : "component_attach";
    if (!g_moduleHost.loaded()) {
        spec.errors_.push_back(spec.session_ + " needs --module <dir>");
        return spec;
    }
    const std::string path =
        (std::filesystem::path(g_moduleHost.dir()) / "session_expect.lua").string();
    auto fail = [&spec, &path](const std::string &why) {
        spec.errors_.push_back(path + ": " + why);
        return spec;
    };
    IRScript::LuaScript &script = g_moduleHost.script();
    sol::protected_function_result result =
        script.lua().safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        const sol::error err = result;
        return fail(err.what());
    }
    const sol::object returned = result;
    if (returned.get_type() != sol::type::table)
        return fail("must return a table");
    const sol::optional<sol::table> entry = returned.as<sol::table>()[entryKey];
    const sol::optional<std::string> component =
        entry ? (*entry)["component"] : sol::optional<std::string>{};
    const sol::optional<std::string> fieldName =
        entry ? (*entry)["field"] : sol::optional<std::string>{};
    if (!component || !fieldName)
        return fail("needs " + entryKey + " = { component, field, value, default }");
    spec.component_ = *component;
    spec.field_ = *fieldName;

    const IRScript::LuaTypedComponentInfo *info =
        IRVoxelEditor::detail::findModuleComponent(script, spec.component_);
    if (info == nullptr)
        return fail("component '" + spec.component_ + "' is not registered by the module");
    const std::vector<std::string> names = componentPaletteNames();
    spec.listRow_ =
        static_cast<int>(std::find(names.begin(), names.end(), spec.component_) - names.begin());
    if (spec.listRow_ * kComponentListItemHeight >= kComponentListSize.y)
        return fail("component '" + spec.component_ + "' is below the list's visible rows");

    // The panel lays a record's fields out in the record's order.
    const ComponentRecord layout = makeComponentRecord(script, spec.component_);
    const auto field =
        std::find_if(layout.fields_.begin(), layout.fields_.end(), [&spec](const auto &f) {
            return f.name_ == spec.field_;
        });
    if (field == layout.fields_.end())
        return fail("component '" + spec.component_ + "' has no field '" + spec.field_ + "'");
    const int fieldIndex = static_cast<int>(field - layout.fields_.begin());
    spec.fieldPage_ = fieldIndex / kComponentFieldRowsPerPage;
    spec.fieldRow_ = fieldIndex % kComponentFieldRowsPerPage;
    if (paged && spec.fieldPage_ == 0)
        return fail("field '" + spec.field_ + "' is on the first page of the field area");
    if (keyed && IRScript::isLuaIdentifier(spec.field_))
        return fail("field '" + spec.field_ + "' is a Lua identifier");

    spec.value_ = IRVoxelEditor::detail::fieldValueFromRow(field->type_, (*entry)["value"]);
    spec.default_ = IRVoxelEditor::detail::fieldValueFromRow(field->type_, (*entry)["default"]);
    if (std::holds_alternative<std::monostate>(spec.value_) ||
        std::holds_alternative<std::monostate>(spec.default_))
        return fail(
            "value and default must be " + std::string(IRScript::toString(field->type_)) +
            " values the panel can type"
        );
    if (spec.value_ == spec.default_)
        return fail("value equals the field's default");
    spec.typedText_ = formatFieldValue(spec.value_);
    if (parseFieldText(field->type_, spec.typedText_) != std::optional{spec.value_})
        return fail("value '" + spec.typedText_ + "' does not type back to itself");
    return spec;
}

// Resolves module_loaded's expectations from <module dir>/session_expect.lua
// against the loaded module: the recipe's list row and slider ranges, and its
// cells at the session's values and at its defaults.
Session::ModuleSessionSpec resolveModuleSessionSpec() {
    Session::ModuleSessionSpec spec;
    if (!g_moduleHost.loaded()) {
        spec.errors_.push_back("module_loaded needs --module <dir>");
        return spec;
    }
    const std::string path =
        (std::filesystem::path(g_moduleHost.dir()) / "session_expect.lua").string();
    auto fail = [&spec, &path](const std::string &why) {
        spec.errors_.push_back(path + ": " + why);
        return spec;
    };
    sol::state &lua = g_moduleHost.script().lua();
    sol::protected_function_result result = lua.safe_script_file(path, sol::script_pass_on_error);
    if (!result.valid()) {
        const sol::error err = result;
        return fail(err.what());
    }
    const sol::object returned = result;
    if (returned.get_type() != sol::type::table)
        return fail("must return a table");
    const sol::table expect = returned.as<sol::table>();

    if (const sol::optional<sol::table> components = expect["components"]) {
        for (std::size_t i = 1; i <= components->size(); ++i) {
            const sol::table entry = (*components)[i];
            const sol::optional<std::string> name = entry["name"];
            const sol::optional<int> fieldCount = entry["fieldCount"];
            if (!name || !fieldCount)
                return fail("each components entry needs name and fieldCount");
            spec.components_.push_back({*name, *fieldCount});
        }
    }
    if (const sol::optional<sol::table> panels = expect["panels"]) {
        for (std::size_t i = 1; i <= panels->size(); ++i) {
            const sol::table entry = (*panels)[i];
            const sol::optional<std::string> name = entry["name"];
            const sol::optional<std::string> label = entry["label"];
            if (!name || !label)
                return fail("each panels entry needs name and label");
            spec.panels_.push_back({*name, *label});
        }
    }

    const sol::optional<sol::table> recipeExpect = expect["recipe"];
    const sol::optional<std::string> recipeName =
        recipeExpect ? (*recipeExpect)["name"] : sol::optional<std::string>{};
    if (!recipeName)
        return fail("needs recipe = { name, values }");
    const std::vector<ModuleRecipe> &recipes = g_moduleHost.recipes();
    const auto recipeIt = std::find_if(recipes.begin(), recipes.end(), [&](const ModuleRecipe &r) {
        return r.name_ == *recipeName;
    });
    if (recipeIt == recipes.end())
        return fail("recipe '" + *recipeName + "' is not registered by the module");
    const std::size_t recipeIndex = static_cast<std::size_t>(recipeIt - recipes.begin());
    spec.recipeRow_ = static_cast<int>(recipeIndex);

    const sol::optional<sol::table> values = (*recipeExpect)["values"];
    std::vector<float> sessionValues;
    std::vector<float> defaultValues;
    for (std::size_t i = 0; i < recipeIt->params_.size(); ++i) {
        const RecipeParam &param = recipeIt->params_[i];
        defaultValues.push_back(param.default_);
        const sol::optional<float> value = values ? (*values)[param.name_] : sol::optional<float>{};
        if (!value) {
            sessionValues.push_back(param.default_);
            continue;
        }
        if (*value < param.min_ || *value > param.max_)
            return fail("value for param '" + param.name_ + "' is outside its range");
        if (*value == param.default_)
            return fail("value for param '" + param.name_ + "' equals its default");
        spec.params_.push_back({static_cast<int>(i), param.min_, param.max_, *value});
        sessionValues.push_back(*value);
    }
    if (spec.params_.empty())
        return fail("recipe.values must move at least one param off its default");

    std::vector<RecipeCell> applied;
    std::vector<RecipeCell> atDefaults;
    std::string error;
    if (!g_moduleHost.evaluate(recipeIndex, sessionValues, g_editableSceneSize, applied, error) ||
        !g_moduleHost.evaluate(recipeIndex, defaultValues, g_editableSceneSize, atDefaults, error))
        return fail(error);
    for (const RecipeCell &cell : applied)
        spec.appliedCells_.push_back(cell.local_);
    for (const RecipeCell &cell : atDefaults) {
        if (std::find(spec.appliedCells_.begin(), spec.appliedCells_.end(), cell.local_) ==
            spec.appliedCells_.end())
            spec.defaultOnlyCells_.push_back(cell.local_);
    }
    if (spec.appliedCells_.empty())
        return fail("recipe '" + *recipeName + "' writes no cells at the session's values");
    return spec;
}

} // namespace IRVoxelEditor

void initSystems();
void initCommands();
void initEntities();

// Lua host for --module, in EVAL mode (hot reload matters here, per-tick cost
// does not). Must register before IREngine::init, which runs the callbacks; a
// registration after init never runs.
void registerLuaBindings() {
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        script.bindLuaDrivenEcs();
        script.registerTypeFromTraits<C_LocalTransform>();
        // initSystems places this instance right after WIDGET_INPUT, where a
        // module's IRGui onClick handlers need it.
        IRVoxelEditor::g_widgetLuaDispatchId =
            script.registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>();
        script.setEcsDefaultMode(IRScript::EcsMode::EVAL);
        IRVoxelEditor::g_moduleHost.bind(script);
    });
}

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: voxel_editor");
    registerLuaBindings();
    IR_LOG_INFO("  Left-drag: AABB box-fill between drag-start and drag-end");
    IR_LOG_INFO("  Shift + left-drag: line-fill along dominant axis");
    IR_LOG_INFO("  Ctrl + left-click: face-fill (flood-fill axis-plane of hit face)");
    IR_LOG_INFO("  Left-click (no drag): place single voxel adjacent to hit face");
    IR_LOG_INFO("  Alt + any place gesture: place on the FAR side of the hit face");
    IR_LOG_INFO("    (on a top face that is BELOW it — the only way to grow downward)");
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
    // Editable grid dims: the ant needs 20³, the tree ~26 tall.
    // Register before init (which owns the parse); read back + derive the origin
    // after. Omitted / non-positive dims keep the historical 16³ scene.
    IREngine::args()
        .numbers("--scene-size", "editable voxel grid dims: W H D (default 16 16 16)", 3);
    // Authoring sessions: replay a recipe of editor gestures
    // through the GUI-test harness instead of the standing shot table. Needs
    // --auto-screenshot as well (that is what wires the harness at all).
    IREngine::args().enumValue(
        "--gui-session",
        "replay an authoring session's scripted gestures: none | drag_probe | place_below | "
        "face_pick | rock | mushroom | ant | bird | tree | parts_roundtrip | tier_scrub | "
        "remove_part | radial_array | nway_symmetry | mode_preview | mode_preview_shots | "
        "module_loaded | "
        "component_attach | component_field_page | component_field_key | "
        "text_input_command_capture",
        {"none",
         "drag_probe",
         "place_below",
         "face_pick",
         "rock",
         "mushroom",
         "ant",
         "bird",
         "tree",
         "parts_roundtrip",
         "remove_part",
         "tier_scrub",
         "radial_array",
         "nway_symmetry",
         "mode_preview",
         "mode_preview_shots",
         "module_loaded",
         "component_attach",
         "component_field_page",
         "component_field_key",
         "text_input_command_capture"},
        "none"
    );
    IREngine::args().string(
        "--module",
        "load a creation module: runs <dir>/init.lua in the editor's Lua VM",
        ""
    );
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
    // A module loads before the session builds: module_loaded resolves its
    // expectations against what the module registered.
    const std::string moduleDir = IREngine::args().getString("--module");
    if (!moduleDir.empty()) {
        std::string error;
        if (!IRVoxelEditor::g_moduleHost.load(moduleDir, error)) {
            IR_LOG_ERROR("Module load failed: {}", error);
            return 2;
        }
        IR_LOG_INFO(
            "module_loaded dir={} components={} recipes={} panels={}",
            moduleDir,
            IRVoxelEditor::g_moduleHost.componentCount(),
            IRVoxelEditor::g_moduleHost.recipes().size(),
            IRVoxelEditor::g_moduleHost.panelNames().size()
        );
    }
    // Build the session recipe before the systems are wired — initSystems hands
    // its shot table to the harness. A recipe that cannot aim one of its
    // gestures is a hard error rather than a partial replay: a session that
    // silently drops an op authors the wrong entity and saves it anyway.
    IRVoxelEditor::g_sessionId =
        IRVoxelEditor::Session::idFromName(IREngine::args().getEnum("--gui-session"));
    if (IRVoxelEditor::g_sessionId != IRVoxelEditor::Session::Id::NONE) {
        const IRVoxelEditor::Session::ModuleSessionSpec moduleSpec =
            IRVoxelEditor::g_sessionId == IRVoxelEditor::Session::Id::MODULE_LOADED
                ? IRVoxelEditor::resolveModuleSessionSpec()
                : IRVoxelEditor::Session::ModuleSessionSpec{};
        const IRVoxelEditor::Session::ComponentAttachSpec componentSpec =
            IRVoxelEditor::Session::isComponentSession(IRVoxelEditor::g_sessionId)
                ? IRVoxelEditor::resolveComponentAttachSpec(IRVoxelEditor::g_sessionId)
                : IRVoxelEditor::Session::ComponentAttachSpec{};
        IRVoxelEditor::g_session = IRVoxelEditor::Session::build(
            IRVoxelEditor::g_sessionId,
            IRVoxelEditor::g_editableSceneSize,
            IRVoxelEditor::g_editableSceneOrigin,
            moduleSpec,
            componentSpec
        );
        if (!IRVoxelEditor::g_session.ok()) {
            for (const std::string &error : IRVoxelEditor::g_session.errors_)
                IR_LOG_ERROR("Session recipe error: {}", error);
            return 1;
        }
        // Resolve the shot table only now that the recipe sits in the storage
        // it keeps for the whole run — the shots point into its segments.
        IRVoxelEditor::Session::resolveShots(IRVoxelEditor::g_session);
        IR_LOG_INFO(
            "Authoring session '{}': {} segments, {} occupancy checks",
            IRVoxelEditor::g_session.name_,
            IRVoxelEditor::g_session.segments_.size(),
            IRVoxelEditor::g_session.checks_.size()
        );
    }
    initSystems();
    initCommands();
    initEntities();
    if (!IRVoxelEditor::initModuleUi())
        return 2;
    IRVoxelEditor::initComponentsUi();
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

    IRVoxelEditor::g_rotationGroupScratch.reserve(IRVoxelEditor::kArrayMaxCount);
    IRVoxelEditor::g_rotationSetScratch.reserve(IRVoxelEditor::kArrayMaxCount);

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
    // over the HELP panel background; reuses the batched GUI-text path.
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

    // Bone-paint selector. Clicking a swatch sets activeBoneIdx_
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
    // Alt modifies any of the place gestures: the hit face's normal inverts, so
    // the edit lands on the far side of the clicked face (editTargetCell).
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
                // Alt inverts the hit-face normal on every place gesture, so the
                // edit lands on the far side of the clicked face (below it, for
                // the -z face). Erase has no far side to invert onto.
                if (!IRVoxelEditor::g_eraseMode &&
                    IRInput::checkKeyMouseModifiers(IRInput::kModifierAlt, 0u)) {
                    status += " BELOW";
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
                if (sym.rotationalOrder_ > 1) {
                    status += " | ROT " + std::to_string(sym.rotationalOrder_);
                }
                IRPrefab::Widget::setLabelText(IRVoxelEditor::g_fillModeLabel, std::move(status));
            }

            bool overWidget = IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.palettePanel_) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_layerPanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_partsPanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_lodPanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayPanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayTypeList) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayCountSlider) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayDistanceSlider) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayStepYSlider) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayStepZSlider) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayYawSlider) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_arrayApplyButton) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_bakePanel) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_bonePaint.bonePanel_) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_skeletonPanel) ||
                              IRVoxelEditor::cursorOverModuleUi();
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

            // A gizmo handle under the cursor owns the click, the same way a
            // widget does: GIZMO_DRAG starts its drag on this press.
            overWidget = overWidget || IRVoxelEditor::cursorOnGizmoHandle();

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
                const auto hit = IRVoxelEditor::pickEditable();
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
            // Alt: place on the far side of the hit face (editTargetCell). Read
            // live here; the drag path latches it at PRESS instead.
            const bool altDown = IRInput::checkKeyMouseModifiers(IRInput::kModifierAlt, 0u);
            const bool leftPressedNow =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
            if (!overWidget && ctrlDown && leftPressedNow) {
                const auto hit = IRVoxelEditor::pickEditable();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    auto &set = IREntity::getComponent<C_VoxelSetNew>(hit->entity_);
                    auto &gpos = IREntity::getComponent<C_WorldTransform>(hit->entity_);
                    // Erase mode floods the hit voxel's own face plane (removing
                    // the exposed layer); place mode floods the empty plane in
                    // front of the hit face, or behind it under Alt. The flood
                    // axis stays the hit normal's axis either way — only which
                    // plane along it the flood starts in moves.
                    const bool erase = IRVoxelEditor::g_eraseMode;
                    const ivec3 faceStart = IRVoxelEditor::editTargetCell(*hit, erase, altDown);
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
                const auto hit = IRVoxelEditor::pickEditable();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    // Erase drags target the hit voxels themselves; place drags
                    // target the cells adjacent to the hit face. Latch Alt here
                    // so the HELD end cell resolves in the same frame's sense.
                    IRVoxelEditor::g_fillTool.dragInverted_ = altDown;
                    const ivec3 startPos =
                        IRVoxelEditor::editTargetCell(*hit, IRVoxelEditor::g_eraseMode, altDown);
                    IRVoxelEditor::g_fillTool.dragging_ = true;
                    IRVoxelEditor::g_fillTool.dragStartWorld_ = startPos;
                    IRVoxelEditor::g_fillTool.dragStartEntity_ = hit->entity_;
                    IRVoxelEditor::g_fillTool.lastEndWorld_ = startPos;
                    IRVoxelEditor::updateGhostShape(startPos, startPos, false);
                }
            }

            if (noCtrl && IRVoxelEditor::g_fillTool.dragging_ &&
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::HELD)) {
                const auto hit = IRVoxelEditor::pickEditable();
                if (hit && hit->faceNormal_ != ivec3(0)) {
                    const ivec3 endPos = IRVoxelEditor::editTargetCell(
                        *hit,
                        IRVoxelEditor::g_eraseMode,
                        IRVoxelEditor::g_fillTool.dragInverted_
                    );
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
        {IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
         IRSystem::createSystem<IRSystem::GATE_VOXEL_SETS_BY_LOD>(),
         IRSystem::createSystem<IRSystem::GIZMO_SCREEN_SPACE_SIZE>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>(),
         IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>(),
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

    // Scrubber + FPS sync. Runs in INPUT, after WIDGET_APPLY_SLIDER
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

    // Frame-based animation playback. Runs once per RENDER tick
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

    // LOD panel sync. Runs in INPUT after WIDGET_APPLY_SLIDER and
    // WIDGET_APPLY_CHECKBOX so this frame's drag or click is committed. A slider
    // acts while pressed and on its release frame; the band sliders keep
    // fine <= coarse by moving the slider not being dragged. The tier pin is
    // staged and flushes at this system's group boundary, before the UPDATE
    // LOD gate reads it.
    auto lodPanelSyncSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorLodPanelSync",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            using namespace IRVoxelEditor;
            if (g_lodPanel == IREntity::kNullEntity)
                return;
            const auto active = [](IREntity::EntityId widget) {
                return IRPrefab::Widget::isPressed(widget) || IRPrefab::Widget::wasClicked(widget);
            };

            if (IRPrefab::Widget::wasClicked(g_lodFollowCheckbox)) {
                setLodTierPin(
                    IRPrefab::Widget::checkboxState(g_lodFollowCheckbox)
                        ? std::nullopt
                        : std::optional{lodLevelFromSlider(g_lodTierSlider)}
                );
            } else if (active(g_lodTierSlider)) {
                setLodTierPin(lodLevelFromSlider(g_lodTierSlider));
            } else if (!g_entityScene.tierOverride()) {
                IRPrefab::Widget::setSliderValue(
                    g_lodTierSlider,
                    static_cast<float>(IRRender::getActiveLodLevel())
                );
            }

            const int selected = g_entityScene.selectedIndex();
            const bool finePressed = active(g_lodFineSlider);
            if (selected < 0 || (!finePressed && !active(g_lodCoarseSlider)))
                return;
            IRRender::LodLevel fine = lodLevelFromSlider(g_lodFineSlider);
            IRRender::LodLevel coarse = lodLevelFromSlider(g_lodCoarseSlider);
            if (fine > coarse) {
                if (finePressed)
                    coarse = fine;
                else
                    fine = coarse;
            }
            g_entityScene.setPartBand(selected, fine, coarse);
            syncLodBandSliders();
        }
    );

    // Layer panel sync. Runs in INPUT after WIDGET_APPLY_LIST and
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
            if (g_entitySceneMode && g_partsList != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_partsList)) {
                const int selected = IRPrefab::Widget::listSelectedIndex(g_partsList);
                if (selected >= 0 && selected < static_cast<int>(g_entityScene.parts().size()) &&
                    selected != g_entityScene.selectedIndex()) {
                    selectEditorPart(selected);
                }
            }
            if (g_entitySceneMode && g_partModeDropdown != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_partModeDropdown)) {
                setSelectedPartRotationMode(rotationModeFromIndex(
                    IRPrefab::Widget::dropdownSelectedIndex(g_partModeDropdown)
                ));
            }
            if (g_entitySceneMode && g_partRemoveButton != IREntity::kNullEntity &&
                IRPrefab::Widget::wasClicked(g_partRemoveButton)) {
                removeSelectedPart();
            }
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

    // Shape bake system: runs in INPUT after WIDGET_APPLY_LIST so the
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

            const int sel = (g_bakeShapeList != IREntity::kNullEntity)
                                ? IRPrefab::Widget::listSelectedIndex(g_bakeShapeList)
                                : kBakeDefaultShapeRow;
            const int idx = (sel >= 0 && sel < static_cast<int>(std::size(kBakeShapeTypes)))
                                ? sel
                                : kBakeDefaultShapeRow;
            const IRPrefab::Prefab::PrefabShapeDescription shape = selectedShapeDescription();

            const float p1 = (g_bakeParam1Slider != IREntity::kNullEntity)
                                 ? IRPrefab::Widget::sliderValue(g_bakeParam1Slider)
                                 : 5.0f;
            const float p2 = (g_bakeParam2Slider != IREntity::kNullEntity)
                                 ? IRPrefab::Widget::sliderValue(g_bakeParam2Slider)
                                 : 3.0f;

            auto &set = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
            IRPrefab::Voxel::fillSdf(
                set,
                shape.type_,
                shape.params_,
                shape.color_,
                true,
                [&](ivec3 local, std::size_t flat, bool place, Color color) {
                    if (flat < set.voxels_.size()) {
                        applyEdit(g_editor.editableVoxelSet_, set, local, flat, place, color);
                    }
                }
            );
            commitStroke(true);
            IR_LOG_INFO("Bake: shape {} P1={:.1f} P2={:.1f}", idx, p1, p2);
        }
    );

    auto arraySystem = IRSystem::createSystem<C_GuiElement>(
        "EditorArray",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            using namespace IRVoxelEditor;
            if (g_arrayApplyButton == IREntity::kNullEntity ||
                !IRPrefab::Widget::wasClicked(g_arrayApplyButton)) {
                return;
            }
            const int count =
                static_cast<int>(IRMath::round(IRPrefab::Widget::sliderValue(g_arrayCountSlider)));
            const float x = IRPrefab::Widget::sliderValue(g_arrayDistanceSlider);
            const int type = IRPrefab::Widget::listSelectedIndex(g_arrayTypeList);
            if (type == 1) {
                applyLinearArray(
                    count,
                    vec3(
                        x,
                        IRPrefab::Widget::sliderValue(g_arrayStepYSlider),
                        IRPrefab::Widget::sliderValue(g_arrayStepZSlider)
                    )
                );
                return;
            }
            const float yaw =
                IRPrefab::Widget::sliderValue(g_arrayYawSlider) * IRMath::kPi / 180.0f;
            applyRadialArray(count, x, yaw);
        }
    );

    // RECIPES panel: a list click re-targets the parameter sliders, APPLY
    // writes the selected recipe's cells. COMPONENTS: ATTACH / DETACH and the
    // field edits. Runs after WIDGET_APPLY_LIST / WIDGET_APPLY_SLIDER /
    // WIDGET_APPLY_TEXT_INPUT so this frame's selection and values are
    // committed.
    auto recipesSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorRecipes",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            IRVoxelEditor::updateRecipesPanel();
            IRVoxelEditor::updateComponentsPanel();
        }
    );

    // Joint-authoring bind-pose sync (placement-vs-posing split). A
    // TRANSLATE_ARROW drag on a rig joint is authoring: recapture
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
                return; // rotate rings are FK posing, not bind authoring
            if (!IRVoxelEditor::isRigJoint(drag->dragAnchor_))
                return; // showcase / non-rig gizmos don't touch the rig
            IRVoxelEditor::recomputeJointBindPose();
        }
    );

    // Skeleton tree panel sync. Runs after WIDGET_APPLY_LIST so
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
         IRVoxelEditor::g_widgetLuaDispatchId,
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_LIST>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
         scrubberSystem,
         layerSyncSystem,
         lodPanelSyncSystem,
         loftInputSystem,
         bakeSystem,
         arraySystem,
         recipesSystem,
         paletteUpdateSystem,
         bonePaintUpdateSystem,
         placeEraseSystem,
         IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create(),
         IRSystem::createSystem<IRSystem::GIZMO_HOVER>(),
         gizmoDragId,
         jointBindSyncSystem,
         jointTreeSyncSystem}
    );

    // GPU voxel-position prepass + joint skin-matrix upload +
    // per-voxel bone→slot seeding — the FK live-deform substrate.
    // UPDATE_JOINT_MATRICES must run AFTER PROPAGATE_TRANSFORM
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
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
            helpRenderSystem,
        }
    );
    // Registry-driven command help overlay: draws every named PRESSED
    // binding, including the ad-hoc lambdas above now that they pass
    // name/description. Must land after TEXT_TO_TRIXEL (already registered
    // above) and before the composite.
    renderPipeline.splice(renderPipeline.end(), IRPrefab::HelpOverlay::systems());
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );

    if (IREngine::args().autoScreenshotWarmupFrames() > 0) {
        IRVideo::GuiTestConfig cfg{};
        cfg.warmupFrames_ = IREngine::args().autoScreenshotWarmupFrames();
        cfg.settleFrames_ = 3;
        // An authoring session replaces the standing table with
        // its own segments; the Recipe owns that storage for the whole run.
        if (IRVoxelEditor::g_sessionId != IRVoxelEditor::Session::Id::NONE) {
            cfg.shots_ = IRVoxelEditor::g_session.shots_.data();
            cfg.numShots_ = static_cast<int>(IRVoxelEditor::g_session.shots_.size());
        } else {
            cfg.shots_ = IRVoxelEditor::kGuiTestShots;
            cfg.numShots_ =
                sizeof(IRVoxelEditor::kGuiTestShots) / sizeof(IRVoxelEditor::kGuiTestShots[0]);
        }
        // Evaluate GUI assertions at each shot's capture frame. The
        // assertion tables themselves are populated later in initEntities (once
        // the widget entities exist), before the game loop fires this callback.
        cfg.onAssertFrame_ = &IRVoxelEditor::onGuiAssertFrame;
        renderPipeline.push_back(IRVideo::createGuiTestSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    // The full camera suite minus Escape→CLOSE_WINDOW, which would conflict
    // with this editor's own drag-cancel handler. The S pan pair rides the
    // suite like every other axis: `CommandManager` decides a bare start/end
    // pair's eligibility once, at the press, so the Ctrl+S save chord shadows
    // both halves or neither, and no per-creation mask is needed.
    IRPrefab::Camera::registerStandardKeyboardCommands({.omit_ = {IRCommand::CLOSE_WINDOW}});

    // F1 opens the registry-driven command help overlay. Every
    // named PRESSED binding registered below appears automatically.
    IRPrefab::HelpOverlay::registerToggleCommand();

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonQ,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q - 1) * IRMath::kHalfPi);
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "YAW CCW",
        "ROTATE CAMERA YAW 90 COUNTERCLOCKWISE"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonE,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q + 1) * IRMath::kHalfPi);
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "YAW CW",
        "ROTATE CAMERA YAW 90 CLOCKWISE"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonSpace,
        []() {
            IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
            IRPrefab::Camera::setYaw(0.0f);
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "RESET CAMERA",
        "RESET PAN AND YAW TO ORIGIN"
    );

    // X/Y/Z: toggle mirror-symmetry axis. When an axis turns ON, seat its mirror
    // plane at the scene centre ((size-1)/2, pairing cell 0 with size-1) so the
    // edit reflection (applyEdit → applyMirrors) lands within [0, size); the
    // default offset 0 would map cell v to -v, out of bounds, dropping every
    // mirror. There is no UI for a non-centre plane, so the centre is the only
    // meaningful position; leaving the offset alone on toggle-OFF keeps a
    // symmetry-disabled scene's saved META (sym_offset_*) untouched.
    auto logSymmetry = []() {
        IR_LOG_INFO(
            "Symmetry: X={} Y={} Z={}",
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
            if (IRVoxelEditor::g_symmetry.enableX_)
                IRVoxelEditor::g_symmetry.offsetX_ =
                    IRVoxelEditor::mirrorCenterOffset(IRVoxelEditor::g_editableSceneSize.x);
            logSymmetry();
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "MIRROR X",
        "TOGGLE X-AXIS MIRROR SYMMETRY"
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonY,
        [logSymmetry]() {
            IRVoxelEditor::g_symmetry.enableY_ = !IRVoxelEditor::g_symmetry.enableY_;
            if (IRVoxelEditor::g_symmetry.enableY_)
                IRVoxelEditor::g_symmetry.offsetY_ =
                    IRVoxelEditor::mirrorCenterOffset(IRVoxelEditor::g_editableSceneSize.y);
            logSymmetry();
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "MIRROR Y",
        "TOGGLE Y-AXIS MIRROR SYMMETRY"
    );
    // Ctrl+Z — undo. Bare Z — toggle Z-mirror. Both share the same key, so the
    // masks below are what disambiguate them: CommandManager fires the
    // modifier-specific binding and suppresses the bare one on the same key.
    // The masks are also what the help overlay renders, so a guard written
    // inline instead would advertise both rows as plain "Z".
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonZ,
        []() { IRVoxelEditor::undoOne(); },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "UNDO",
        "UNDO LAST EDIT"
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonZ,
        [logSymmetry]() {
            IRVoxelEditor::g_symmetry.enableZ_ = !IRVoxelEditor::g_symmetry.enableZ_;
            if (IRVoxelEditor::g_symmetry.enableZ_)
                IRVoxelEditor::g_symmetry.offsetZ_ =
                    IRVoxelEditor::mirrorCenterOffset(IRVoxelEditor::g_editableSceneSize.z);
            logSymmetry();
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "MIRROR Z",
        "TOGGLE Z-AXIS MIRROR SYMMETRY"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonR,
        []() { IRVoxelEditor::applyRadialArray(6, 4.0f, 0.0f); },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "RADIAL ARRAY",
        "MAKE SIX RADIAL COPIES OF THE SELECTED PART"
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonL,
        []() { IRVoxelEditor::applyLinearArray(3, IRMath::vec3(2.0f, 0.0f, 0.0f)); },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "LINEAR ARRAY",
        "MAKE THREE LINEAR COPIES OF THE SELECTED PART"
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonY,
        []() { IRVoxelEditor::toggleRotationalSymmetry(); },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "ROTATIONAL SYMMETRY",
        "TOGGLE N-WAY SYMMETRY FOR THE SELECTED ARRAY GROUP"
    );

    // V — toggle erase-fill mode: the left-click place / box /
    // line / face gestures ERASE instead of place while it is on. Right-click
    // single-voxel erase is unaffected.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonV,
        []() {
            IRVoxelEditor::g_eraseMode = !IRVoxelEditor::g_eraseMode;
            IR_LOG_INFO("Erase-fill mode: {}", IRVoxelEditor::g_eraseMode ? "ON" : "OFF");
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "ERASE MODE",
        "TOGGLE ERASE-FILL MODE"
    );

    // Frame-based animation controls. Keys not taken by the gizmo
    // controls (Q/E/Space/Z), symmetry (X/Y/Z), or the layer system
    // (K/[/]/H): Left/Right for frame nav, P play/pause, A add
    // blank frame, D duplicate, Backspace delete, L loop-mode toggle.

    // Left arrow — go to previous frame.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonLeft,
        []() { IRVoxelEditor::switchToFrame(IRVoxelEditor::g_anim.activeFrame_ - 1); },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "PREV FRAME",
        "GO TO PREVIOUS ANIMATION FRAME"
    );

    // Right arrow — go to next frame.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonRight,
        []() { IRVoxelEditor::switchToFrame(IRVoxelEditor::g_anim.activeFrame_ + 1); },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "NEXT FRAME",
        "GO TO NEXT ANIMATION FRAME"
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
                "Playback: {}  ({} frames at {:.0f} FPS)",
                anim.playing_ ? "PLAYING" : "PAUSED",
                anim.frameCount(),
                anim.fps_
            );
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "PLAY/PAUSE",
        "TOGGLE FRAME PLAYBACK"
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
            IR_LOG_INFO("Added blank frame {} / {}", anim.activeFrame_ + 1, anim.frameCount());
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "ADD FRAME",
        "ADD BLANK FRAME AFTER CURRENT"
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
            IR_LOG_INFO("Duplicated frame {} / {}", anim.activeFrame_ + 1, anim.frameCount());
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "DUPLICATE FRAME",
        "DUPLICATE THE CURRENT FRAME"
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
            IR_LOG_INFO("Deleted frame (now {} / {})", anim.activeFrame_ + 1, anim.frameCount());
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "DELETE FRAME",
        "DELETE THE CURRENT FRAME"
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
                "Loop mode: {}",
                anim.loopMode_ == IRVoxelEditor::LoopMode::LOOP ? "LOOP" : "PING-PONG"
            );
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "LOOP MODE",
        "TOGGLE LOOP / PING-PONG PLAYBACK"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "CANCEL / QUIT",
        "CANCEL DRAG OR CLOSE WINDOW"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "LOFT MODE",
        "TOGGLE LOFT MASK MODE"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "STAMP LOFT",
        "STAMP LOFT MASKS INTO SCENE"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "CLEAR LOFT",
        "CLEAR LOFT MASKS"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "ADD LAYER",
        "ADD A NEW LAYER"
    );

    // [: select previous layer in display order (wraps around)
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonLeftBracket,
        []() {
            IRVoxelEditor::g_layerManager.selectPrevLayer();
            IRVoxelEditor::logLayerState();
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "PREV LAYER",
        "SELECT PREVIOUS LAYER"
    );

    // ]: select next layer in display order (wraps around)
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonRightBracket,
        []() {
            IRVoxelEditor::g_layerManager.selectNextLayer();
            IRVoxelEditor::logLayerState();
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "NEXT LAYER",
        "SELECT NEXT LAYER"
    );

    // J — toggle skeletal joint-authoring mode. While on, B adds a
    // joint and R starts a fresh chain off the rig root.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonJ,
        []() {
            IRVoxelEditor::g_jointTool.active_ = !IRVoxelEditor::g_jointTool.active_;
            IR_LOG_INFO("Joint authoring: {}", IRVoxelEditor::g_jointTool.active_ ? "ON" : "OFF");
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "JOINT MODE",
        "TOGGLE JOINT AUTHORING MODE"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "ADD JOINT",
        "ADD JOINT TO ACTIVE CHAIN"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "NEW CHAIN",
        "START A NEW BONE CHAIN"
    );

    // N — toggle bone-paint mode. While on, left-click writes
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "BONE PAINT",
        "TOGGLE BONE PAINT MODE"
    );

    // T — set current pose as bind: the posed joint chain becomes
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "SET BIND POSE",
        "SET CURRENT POSE AS BIND"
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
        },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "TOGGLE LAYER VIS",
        "TOGGLE ACTIVE LAYER VISIBILITY"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonP,
        []() { IRVoxelEditor::addEditorVoxelPart(); },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "ADD PART",
        "ENTER ENTITY SCENE MODE AND ADD A VOXEL PART"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonP,
        []() { IRVoxelEditor::addEditorShapePart(); },
        IRInput::kModifierControl | IRInput::kModifierShift,
        IRInput::kModifierNone,
        "ADD SHAPE PART",
        "ADD THE BAKE PANEL'S SDF PRIMITIVE AS AN ENTITY-SCENE PART"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonM,
        []() { IRVoxelEditor::cycleSelectedPartRotationMode(); },
        IRInput::kModifierNone,
        IRInput::kModifierControl,
        "CYCLE PART MODE",
        "CYCLE GRID, DETACHED, AND DETACHED REVOXELIZE"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonM,
        []() { IRVoxelEditor::toggleModePreviewTwin(); },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "TOGGLE MODE PREVIEW",
        "SHOW THE SELECTED PART BESIDE ITS NEXT RENDER MODE"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonDelete,
        []() { IRVoxelEditor::removeSelectedPart(); },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "REMOVE PART",
        "REMOVE THE SELECTED ENTITY-SCENE PART"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonTab,
        []() { IRVoxelEditor::selectRelativeEditorPart(1); },
        IRInput::kModifierNone,
        IRInput::kModifierShift,
        "NEXT PART",
        "SELECT THE NEXT ENTITY-SCENE PART"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonTab,
        []() { IRVoxelEditor::selectRelativeEditorPart(-1); },
        IRInput::kModifierShift,
        IRInput::kModifierNone,
        "PREVIOUS PART",
        "SELECT THE PREVIOUS ENTITY-SCENE PART"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonComma,
        []() { IRVoxelEditor::stepLodTierPin(-1); },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "FINER TIER",
        "PIN THE LOD PREVIEW ONE TIER FINER"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonPeriod,
        []() { IRVoxelEditor::stepLodTierPin(1); },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "COARSER TIER",
        "PIN THE LOD PREVIEW ONE TIER COARSER"
    );

    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonBackspace,
        []() {
            if (IRVoxelEditor::g_entitySceneMode) {
                IRVoxelEditor::clearEntitySceneForLoad();
            }
        },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "CLEAR ENTITY SCENE",
        "CLEAR PARTS WHILE KEEPING ENTITY-SCENE MODE"
    );

    // Ctrl+S — save scene (all frames + layer metadata) to disk.
    // Snapshots the live voxels into the active frame before writing.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonS,
        []() {
            if (IRVoxelEditor::g_entitySceneMode) {
                const auto result = IRVoxelEditor::g_entityScene.save(
                    std::string(IRVoxelEditor::kSceneSaveDir),
                    std::string(IRVoxelEditor::kSceneBaseName)
                );
                if (result.ok_) {
                    IR_LOG_INFO(
                        "Entity scene saved to {}/{}.prefab.lua",
                        IRVoxelEditor::kSceneSaveDir,
                        IRVoxelEditor::kSceneBaseName
                    );
                } else {
                    IR_LOG_ERROR("Entity scene save failed: {}", result.error_);
                }
                return;
            }
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
        },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "SAVE SCENE",
        "SAVE ALL FRAMES AND LAYERS"
    );

    // Ctrl+Shift+S — save skeleton to {kSceneSaveDir}/{kSceneBaseName}.rig.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonS,
        []() {
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
        },
        IRInput::kModifierControl | IRInput::kModifierShift,
        IRInput::kModifierNone,
        "SAVE RIG",
        "SAVE SKELETON TO .RIG FILE"
    );

    // Ctrl+O — load scene from disk, replacing all frames and layer state.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonO,
        []() {
            if (IRVoxelEditor::g_entitySceneMode) {
                const auto loaded = IRVoxelEditor::g_entityScene.load(
                    IRVoxelEditor::g_moduleHost.script(),
                    std::string(IRVoxelEditor::kSceneSaveDir),
                    std::string(IRVoxelEditor::kSceneBaseName)
                );
                if (!loaded.ok_) {
                    IR_LOG_ERROR("Entity scene load failed: {}", loaded.error_);
                    return;
                }
                IRVoxelEditor::clearUndoHistory();
                IRVoxelEditor::selectEditorPart(IRVoxelEditor::g_entityScene.selectedIndex());
                IR_LOG_INFO(
                    "Entity scene loaded: {} parts",
                    IRVoxelEditor::g_entityScene.parts().size()
                );
                return;
            }
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
        },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "LOAD SCENE",
        "LOAD SCENE, REPLACE ALL FRAMES"
    );

    // Ctrl+Shift+O — load skeleton from {kSceneSaveDir}/{kSceneBaseName}.rig.
    // Destroys existing joint entities and their gizmo children, then
    // reconstructs the skeleton from the saved .rig file.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonO,
        []() {
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
                IRPrefab::Gizmo::destroyForAnchors(oldJointIds);
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
        },
        IRInput::kModifierControl | IRInput::kModifierShift,
        IRInput::kModifierNone,
        "LOAD RIG",
        "LOAD SKELETON FROM .RIG FILE"
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

    // An entity-authoring session builds on a bare stage — nothing but the
    // editable set — so its captures show the entity alone and no gizmo handle
    // is drawn over a cell the recipe clicks. The edit pick passes through the
    // reference furniture below (floor slab, axis bars, centre cube, perimeter
    // gizmos, starter rig, satellite sets) either way; a session that proves
    // exactly that asks for it with referenceFurniture_.
    const bool sessionScene = IRVoxelEditor::g_sessionId != IRVoxelEditor::Session::Id::NONE &&
                              !IRVoxelEditor::g_session.referenceFurniture_;

    constexpr float kFloorZ = 2.0f;

    if (!sessionScene) {
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
    }

    // Gizmo primitives around the perimeter: visual references for the
    // gizmo render pass.
    if (!sessionScene) {
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

    // Joint-authoring starter rig — a short bone chain so the
    // feature is visible on launch and in auto-screenshots (same spirit as the
    // perimeter gizmo references above). Author more with J (toggle mode) + B
    // (add joint); R starts a new chain off the rig root.
    if (!sessionScene)
        IRVoxelEditor::seedDemoSkeleton();

    // Editable voxel set — the place/erase target. Allocated empty
    // (default color, alpha=255 so cells are active at start) then
    // every voxel is deactivated so the user starts from an empty
    // scene. A single floor row stays activated as a "ground" for the
    // first click to land on. The size is a named constant on the
    // editor side, not hardcoded inline.
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

    // Smaller satellite voxel sets — reference furniture a click passes
    // through, with fixed colors so they read as distinct from the editable
    // set.
    if (!sessionScene) {
        IREntity::createEntity(
            C_LocalTransform{vec3(-16.0f, 0.0f, -6.0f)},
            C_VoxelSetNew{ivec3(4, 4, 4), Color{120, 180, 240, 255}},
            C_EditorReference{}
        );
        IREntity::createEntity(
            C_LocalTransform{vec3(16.0f, 0.0f, -6.0f)},
            C_VoxelSetNew{ivec3(4, 4, 4), Color{240, 180, 120, 255}},
            C_EditorReference{}
        );
    }

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
    // swatches lay out in a 4×4 grid below the title. The palette is
    // mutable and voxels store raw RGBA, so editing a swatch does not
    // repaint already-placed voxels.
    // Palette docks to the bottom-left of the GUI canvas so it sits
    // below the iso scene render and never covers the edit target.
    // Sized to fit a 4×4 grid of 22-trixel swatches inside a
    // ~115×165-trixel panel — small enough not to crowd the workspace.
    g_editor.palettePanel_ = IRPrefab::Widget::makePanel(
        IRVoxelEditor::kPalettePanelPos,
        IRVoxelEditor::kPalettePanelSize,
        "PALETTE"
    );
    // makePanel skips C_HitBox2DGui so it doesn't consume mouse hover. Add it
    // manually so clicks on the panel background (title bar, label gap, padding)
    // are blocked from falling through to the scene picker.
    IREntity::setComponent(
        g_editor.palettePanel_,
        IRComponents::C_HitBox2DGui{IRVoxelEditor::kPalettePanelSize}
    );
    // Left-align the hint to the panel padding so it doesn't overflow the
    // right edge ("CLICK A SWATCH" is ~111 trixels; the panel interior is ~116).
    IRPrefab::Widget::makeLabel(
        ivec2(IRVoxelEditor::kPalettePanelPos.x + 4, IRVoxelEditor::kPalettePanelPos.y + 36),
        "CLICK A SWATCH"
    );

    g_editor.paletteSwatches_.reserve(IRVoxelEditor::kPaletteCount);
    for (int i = 0; i < IRVoxelEditor::kPaletteCount; ++i) {
        g_editor.paletteSwatches_.push_back(
            IRPrefab::Widget::makeColorSwatch(
                IRVoxelEditor::paletteSwatchPos(i),
                ivec2(IRVoxelEditor::kPaletteSwatchSize),
                IRVoxelEditor::kPaletteColors[i],
                i == 0
            )
        );
    }

    // Animation controls panel — sits below the palette
    // panel. Frame scrubber: drag to navigate frames smoothly. FPS slider:
    // drag to adjust playback speed (1–30 FPS). Both update in real time;
    // keyboard nav (Left/Right) and playback keep the scrubber thumb in sync.
    IRPrefab::Widget::makePanel(
        IRVoxelEditor::kAnimPanelPos,
        IRVoxelEditor::kAnimPanelSize,
        "ANIM"
    );
    IRVoxelEditor::g_scrubberSlider = IRPrefab::Widget::makeSlider(
        IRVoxelEditor::kScrubberSliderGeometry.pos_,
        IRVoxelEditor::kScrubberSliderGeometry.size_,
        "FRAME",
        IRVoxelEditor::kScrubberSliderMinValue,
        static_cast<float>(IRVoxelEditor::g_anim.frameCount() - 1),
        0.0f
    );
    IRVoxelEditor::g_fpsSlider = IRPrefab::Widget::makeSlider(
        IRVoxelEditor::kFpsSliderGeometry.pos_,
        IRVoxelEditor::kFpsSliderGeometry.size_,
        "FPS",
        IRVoxelEditor::kFpsSliderMinValue,
        IRVoxelEditor::kFpsSliderMaxValue,
        IRVoxelEditor::g_anim.fps_
    );

    // Layer panel — second column alongside the PALETTE panel.
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

    constexpr ivec2 kPartsPanelPos{254, 240};
    constexpr ivec2 kPartsPanelSize{120, 116};
    IRVoxelEditor::g_partsPanel =
        IRPrefab::Widget::makePanel(kPartsPanelPos, kPartsPanelSize, "PARTS");
    IREntity::setComponent(
        IRVoxelEditor::g_partsPanel,
        IRComponents::C_HitBox2DGui{kPartsPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_partsPanel).zOrder_ = -1;
    IRVoxelEditor::g_partsList = IRPrefab::Widget::makeList(
        ivec2(kPartsPanelPos.x + 4, kPartsPanelPos.y + 18),
        ivec2(112, 44),
        {},
        -1,
        13
    );
    IRVoxelEditor::g_partModeDropdown = IRPrefab::Widget::makeDropdown(
        ivec2(kPartsPanelPos.x + 4, kPartsPanelPos.y + 66),
        ivec2(112, 18),
        {"GRID", "DETACHED", "DETACHED REVOX"},
        0,
        16
    );
    IRVoxelEditor::g_partRemoveButton = IRPrefab::Widget::makeButton(
        ivec2(kPartsPanelPos.x + 4, kPartsPanelPos.y + 88),
        ivec2(112, 18),
        "REMOVE"
    );
    IRPrefab::Widget::setDisabled(IRVoxelEditor::g_partRemoveButton, true);

    IRVoxelEditor::g_lodPanel = IRPrefab::Widget::makePanel(
        IRVoxelEditor::kLodPanelPos,
        IRVoxelEditor::kLodPanelSize,
        "LOD"
    );
    IREntity::setComponent(
        IRVoxelEditor::g_lodPanel,
        IRComponents::C_HitBox2DGui{IRVoxelEditor::kLodPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_lodPanel).zOrder_ = -1;
    const auto makeLodSlider = [](const IRVoxelEditor::SliderGeometry &geom,
                                  std::string label,
                                  IRRender::LodLevel initial) {
        return IRPrefab::Widget::makeSlider(
            geom.pos_,
            geom.size_,
            std::move(label),
            IRVoxelEditor::kLodTierSliderMin,
            IRVoxelEditor::kLodTierSliderMax,
            static_cast<float>(initial)
        );
    };
    IRVoxelEditor::g_lodFineSlider =
        makeLodSlider(IRVoxelEditor::kLodFineSliderGeometry, "FINE", IRRender::LodLevel::LOD_0);
    IRVoxelEditor::g_lodCoarseSlider =
        makeLodSlider(IRVoxelEditor::kLodCoarseSliderGeometry, "COARSE", IRRender::LodLevel::LOD_4);
    IRVoxelEditor::g_lodTierSlider =
        makeLodSlider(IRVoxelEditor::kLodTierSliderGeometry, "TIER", IRRender::LodLevel::LOD_4);
    IRVoxelEditor::g_lodFollowCheckbox = IRPrefab::Widget::makeCheckbox(
        IRVoxelEditor::kLodFollowCheckboxPos,
        IRVoxelEditor::kLodFollowCheckboxSize,
        "FOLLOW ZOOM",
        true
    );
    IRVoxelEditor::syncLodBandSliders();

    IRVoxelEditor::g_arrayPanel = IRPrefab::Widget::makePanel(
        IRVoxelEditor::kArrayPanelPos,
        IRVoxelEditor::kArrayPanelSize,
        "ARRAY"
    );
    IREntity::setComponent(
        IRVoxelEditor::g_arrayPanel,
        IRComponents::C_HitBox2DGui{IRVoxelEditor::kArrayPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_arrayPanel).zOrder_ = -1;
    IRVoxelEditor::g_arrayTypeList = IRPrefab::Widget::makeList(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 18),
        ivec2(122, 28),
        {"RADIAL", "LINEAR"},
        0,
        13
    );
    IRVoxelEditor::g_arrayCountSlider = IRPrefab::Widget::makeSlider(
        IRVoxelEditor::kArrayCountSliderGeometry.pos_,
        ivec2(122, 14),
        "COUNT",
        2.0f,
        static_cast<float>(IRVoxelEditor::kArrayMaxCount),
        6.0f
    );
    IRVoxelEditor::g_arrayDistanceSlider = IRPrefab::Widget::makeSlider(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 68),
        ivec2(122, 14),
        "RADIUS/X",
        -12.0f,
        12.0f,
        4.0f
    );
    IRVoxelEditor::g_arrayStepYSlider = IRPrefab::Widget::makeSlider(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 86),
        ivec2(122, 14),
        "STEP Y",
        -12.0f,
        12.0f,
        0.0f
    );
    IRVoxelEditor::g_arrayStepZSlider = IRPrefab::Widget::makeSlider(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 104),
        ivec2(122, 14),
        "STEP Z",
        -12.0f,
        12.0f,
        0.0f
    );
    IRVoxelEditor::g_arrayYawSlider = IRPrefab::Widget::makeSlider(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 122),
        ivec2(122, 14),
        "COPY YAW",
        -180.0f,
        180.0f,
        0.0f
    );
    IRVoxelEditor::g_arrayApplyButton = IRPrefab::Widget::makeButton(
        ivec2(IRVoxelEditor::kArrayPanelPos.x + 4, IRVoxelEditor::kArrayPanelPos.y + 142),
        ivec2(122, 14),
        "APPLY"
    );

    // Parametric shape bake panel. Sits below the LAYERS panel.
    // Shape list selects the SDF primitive; P1/P2 sliders set the primary and
    // secondary params; BAKE writes DENSE voxels into the active entity.
    // List itemHeight is one glyph row + 2-trixel gap so the 6 shape rows
    // don't touch (itemHeight == glyph height made adjacent rows overlap). The
    // sub-controls sit below the now-taller 6-row list (18 + 6*13 = 96).
    using IRVoxelEditor::kBakePanelPos;
    using IRVoxelEditor::kBakePanelSize;
    IRVoxelEditor::g_bakePanel = IRPrefab::Widget::makePanel(kBakePanelPos, kBakePanelSize, "BAKE");
    IREntity::setComponent(IRVoxelEditor::g_bakePanel, IRComponents::C_HitBox2DGui{kBakePanelSize});
    IRVoxelEditor::g_bakeShapeList = IRPrefab::Widget::makeList(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 18),
        ivec2(112, 78),
        {"BOX", "SPHERE", "CYLINDER", "TORUS", "CONE", "ELLIPSOID"},
        IRVoxelEditor::kBakeDefaultShapeRow,
        13
    );
    IRVoxelEditor::g_bakeParam1Slider = IRPrefab::Widget::makeSlider(
        IRVoxelEditor::kBakeParam1SliderGeometry.pos_,
        IRVoxelEditor::kBakeParam1SliderGeometry.size_,
        "P1",
        IRVoxelEditor::kBakeParamSliderMin,
        IRVoxelEditor::kBakeParamSliderMax,
        IRVoxelEditor::kBakeParam1Default
    );
    IRVoxelEditor::g_bakeParam2Slider = IRPrefab::Widget::makeSlider(
        IRVoxelEditor::kBakeParam2SliderGeometry.pos_,
        IRVoxelEditor::kBakeParam2SliderGeometry.size_,
        "P2",
        IRVoxelEditor::kBakeParamSliderMin,
        IRVoxelEditor::kBakeParamSliderMax,
        IRVoxelEditor::kBakeParam2Default
    );
    IRVoxelEditor::g_bakeButton = IRPrefab::Widget::makeButton(
        IRVoxelEditor::kBakeButtonPos,
        IRVoxelEditor::kBakeButtonSize,
        "BAKE"
    );

    // Bone selector panel. kBoneSwatchCount swatches in a 2×4
    // grid; index 0 = identity (gray), indices 1..7 cycle through distinct hues.
    // Clicking a swatch sets g_bonePaint.activeBoneIdx_; N enables bone-paint mode.
    // Third column (x=256) atop the SKELETON panel — mirrors the LAYERS/BAKE
    // stack in column two so both bone panels stay on-screen.
    constexpr ivec2 kBonePanelPos{378, 240};
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

    // Skeleton tree panel. Sits below the BONE selector in the
    // third column (mirrors LAYERS→BAKE in column two), so the swatch grid and
    // the joint tree coexist without overlapping.
    // Shows the live joint list from C_Skeleton.joints_; clicking a row
    // selects that joint as the active bone (for B-chaining). The rename
    // row writes C_JointName; the reparent row rewrites the CHILD_OF
    // relation and updates parentIdx_ + bindPose_.
    IRVoxelEditor::g_skeletonPanel = IRPrefab::Widget::makePanel(
        IRVoxelEditor::Session::kSkeletonPanelPos,
        IRVoxelEditor::Session::kSkeletonPanelSize,
        "SKELETON"
    );
    IREntity::setComponent(
        IRVoxelEditor::g_skeletonPanel,
        IRComponents::C_HitBox2DGui{IRVoxelEditor::Session::kSkeletonPanelSize}
    );
    IREntity::getComponent<IRComponents::C_Widget>(IRVoxelEditor::g_skeletonPanel).zOrder_ = -1;

    IRVoxelEditor::g_skeletonList = IRPrefab::Widget::makeList(
        ivec2(
            IRVoxelEditor::Session::kSkeletonPanelPos.x + 4,
            IRVoxelEditor::Session::kSkeletonPanelPos.y + 18
        ),
        ivec2(112, 52),
        {},
        -1,
        13
    );
    IRVoxelEditor::g_jointRenameInput = IRPrefab::Widget::makeTextInput(
        IRVoxelEditor::Session::kJointRenameInputPos,
        IRVoxelEditor::Session::kJointRenameInputSize,
        "",
        24
    );
    IRVoxelEditor::g_jointRenameBtn = IRPrefab::Widget::makeButton(
        ivec2(
            IRVoxelEditor::Session::kSkeletonPanelPos.x + 90,
            IRVoxelEditor::Session::kSkeletonPanelPos.y + 74
        ),
        ivec2(26, 14),
        "REN"
    );
    IRVoxelEditor::g_jointReparentInput = IRPrefab::Widget::makeTextInput(
        ivec2(
            IRVoxelEditor::Session::kSkeletonPanelPos.x + 4,
            IRVoxelEditor::Session::kSkeletonPanelPos.y + 92
        ),
        ivec2(82, 14),
        "-1",
        4
    );
    IRVoxelEditor::g_jointReparentBtn = IRPrefab::Widget::makeButton(
        ivec2(
            IRVoxelEditor::Session::kSkeletonPanelPos.x + 90,
            IRVoxelEditor::Session::kSkeletonPanelPos.y + 92
        ),
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
        {IRVoxelEditor::g_partsPanel,
         "PARTS: Ctrl+P adds voxels; Ctrl+Shift+P adds an SDF; Tab selects."},
        {IRVoxelEditor::g_partsList, "PARTS: select the voxel set or shape edited in place."},
        {IRVoxelEditor::g_lodPanel, "LOD: the selected part's tier band and the tier preview."},
        {IRVoxelEditor::g_lodFineSlider, "FINE: finest tier (0..4) the selected part exists at."},
        {IRVoxelEditor::g_lodCoarseSlider, "COARSE: coarsest tier the selected part exists at."},
        {IRVoxelEditor::g_lodTierSlider, "TIER: preview the parts at a tier (, and . step it)."},
        {IRVoxelEditor::g_lodFollowCheckbox, "FOLLOW ZOOM: preview the camera zoom's own tier."},
        {IRVoxelEditor::g_arrayPanel,
         "ARRAY: radial or linear part copies; each remains editable."},
        {IRVoxelEditor::g_arrayApplyButton, "APPLY: create the configured part array as one undo."},
        {IRVoxelEditor::g_partModeDropdown,
         "MODE: preview GRID, DETACHED, or DETACHED REVOXELIZE (M cycles)."},
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

    const bool modePreviewSession =
        IRVoxelEditor::g_sessionId == IRVoxelEditor::Session::Id::MODE_PREVIEW ||
        IRVoxelEditor::g_sessionId == IRVoxelEditor::Session::Id::MODE_PREVIEW_SHOTS;
    if (modePreviewSession) {
        IREntity::destroyEntity(g_editor.editableVoxelSet_);
        IRVoxelEditor::g_entitySceneMode = true;
        IRVoxelEditor::g_entityScene.begin();
        constexpr ivec3 kPreviewSize{6, 6, 6};
        const IREntity::EntityId part =
            IRVoxelEditor::g_entityScene
                .addVoxelPart(kPreviewSize, vec3(0.0f), Color{150, 205, 235, 255}, true);
        auto &set = IREntity::getComponent<C_VoxelSetNew>(part);
        set.carve([](vec3 local) {
            return local.x >= 1.5f && local.y <= -0.5f && local.z <= 0.5f;
        });
        IREntity::getComponent<C_LocalTransform>(part).rotation_ =
            IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kPi / 6.0f);
        IRVoxelEditor::selectEditorPart(0, false);
        g_editor.editableVoxelSet_ = part;
        IRVoxelEditor::g_sceneVoxelSetEntity = part;
        IRVoxelEditor::g_modePreviewBaseCanvasCount = IRPrefab::EntityCanvas::count();
    }

    // GUI-test assertions — populated here (not at the constexpr
    // shot table) because they reference runtime widget EntityIds. The
    // hover-export singleton (one per world) lets HOVERS read the topmost
    // hovered widget via WIDGET_INPUT::endTick. The scripted GUI-assert shot
    // parks the cursor on the layer list and clicks; the pick shot casts a ray
    // onto the scene. PICKS_VOXEL's expected voxel is the regression baseline
    // for screen→world picking alignment.
    IRPrefab::Widget::makeGuiHoverState();
    // A session drives its own per-segment assertion tables (built with the
    // recipe, evaluated through onSessionAssertFrame), so the standing tables
    // below are only wired for the standing shot table.
    if (sessionScene)
        return;
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
    // Each probe-map shot asserts the ray landed on the
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
    // Erase-fill: the erase-fill toggle's mode + status-label check. A
    // PREDICATE assertion so it shares the harness's single GUI-ASSERT emitter
    // with every other kind.
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kProbeEraseShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateEraseModeLabel,
            nullptr,
            "v_toggles_erase_mode"
        ),
    };
    // The help overlay, opened by F1 on its own shot: it is actually
    // visible, it actually batched glyphs, and the text it built advertises every
    // modifier-bearing binding with its real chord. The closed shot is the
    // negative half — the toggle releases the overlay and the glyph count falls
    // back to 0, which is also what leaves the probe shots below unperturbed.
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kHelpOverlayOpenShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateOverlayVisibility,
            &IRVoxelEditor::kOverlayExpectVisible,
            "overlay_visible"
        ),
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateOverlayGlyphsBatched,
            &IRVoxelEditor::kOverlayExpectVisible,
            "overlay_glyphs_batched"
        ),
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateChordOverlayRows,
            nullptr,
            "overlay_chord_rows"
        ),
    };
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kHelpOverlayClosedShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateOverlayVisibility,
            &IRVoxelEditor::kOverlayExpectHidden,
            "overlay_hidden"
        ),
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateOverlayGlyphsBatched,
            &IRVoxelEditor::kOverlayExpectHidden,
            "overlay_no_glyphs_batched"
        ),
    };
    // The Ctrl+S probe must leave the camera's start/end velocity pair
    // balanced. Asserted on the save probe's own capture frame, four frames
    // after its last event, so both halves have drained.
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kProbeSaveShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateCameraVelocityBalanced,
            nullptr,
            "ctrl_s_leaves_camera_balanced"
        ),
    };
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kProbeSaveCtrlFirstShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluateCtrlFirstReleaseBalanced,
            nullptr,
            "ctrl_released_first_leaves_camera_balanced"
        ),
    };
    IRVoxelEditor::g_shotAssertions[IRVoxelEditor::kProbePanThenCtrlShotIndex] = {
        IRPrefab::GuiTest::predicate(
            &IRVoxelEditor::evaluatePanThenCtrlBalanced,
            nullptr,
            "bare_pan_then_ctrl_pans_then_stops"
        ),
    };
}
