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
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>

// Gizmo primitives (T-152, F-0.5 Phase 1)
#include <irreden/render/gizmo.hpp>

// Picking + ray-hit struct (T-219)
#include <irreden/render/picking.hpp>

// Widget framework (T-145 / T-177)
#include <irreden/render/widgets.hpp>

// Systems
#include <irreden/update/systems/system_update_positions_global.hpp>
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
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_gizmo_hover.hpp>
#include <irreden/render/systems/system_gizmo_drag.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_color_swatch.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>

// Camera prefab namespace (Z-yaw API)
#include <irreden/render/camera.hpp>

// Command suites
#include <irreden/common/command_suite_camera.hpp>

// Frame-based animation state (T-214, F-1.4)
#include "animation.hpp"

#include "editor_layer_manager.hpp"

#include <algorithm>
#include <cstddef>
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

// Loft-tool mask rendering (trixel_rect fillRect, trixel_text renderText,
// mask_grid_painter drawMaskGridOntoCanvas + hitTestGridCell,
// layout mouse-in-GUI-trixels helper).
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/mask_grid_painter.hpp>
#include <irreden/render/layout.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRVoxelEditor {

// Scene + palette config (kept as named constants so the editor never
// inlines a hardcoded dimension — D1 in the T-211 architect direction
// calls out that the size be configurable per scene).
constexpr ivec3 kEditableSceneSize{16, 16, 16};
constexpr vec3 kEditableSceneOrigin{-8.0f, -8.0f, -12.0f};
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

struct ScrollZoomParams {
    IREntity::EntityId cameraEntity_ = IREntity::kNullEntity;
    int scrollDelta_ = 0;
};

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

// Three auto-screenshot framings so the render-debug-loop / regression
// harness records the palette panel and the editable scene from a
// stable camera. Camera position is irrelevant to the GUI canvas but
// it does anchor the world-space scene render.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, IRMath::vec2(0.0f, 0.0f), "editor_idle"},
    {0.75f, IRMath::vec2(0.0f, 0.0f), "editor_zoom_out"},
    {1.5f, IRMath::vec2(0.0f, 0.0f), "editor_zoom_in"},
};

int g_autoWarmupFrames = 0;

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
// active fill mode (BOX / LINE / FACE) and active symmetry axes.
IREntity::EntityId g_fillModeLabel = IREntity::kNullEntity;

// Parametric shape bake panel widget entity IDs (T-286).
IREntity::EntityId g_bakePanel = IREntity::kNullEntity;
IREntity::EntityId g_bakeShapeList = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam1Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeParam2Slider = IREntity::kNullEntity;
IREntity::EntityId g_bakeButton = IREntity::kNullEntity;

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
    for (auto &v : set.voxels_) {
        if (v.layer_id_ != layerId)
            continue;
        if (visible)
            v.activate();
        else
            v.deactivate();
    }
    set.syncActiveMask();
    IRPrefab::Voxel::recomputeFaceOccupancy(set.voxels_, set.size_);
}

// Apply a single placement / erasure edit to the voxel set, appending
// the prior state to the in-flight stroke buffer. Skips the edit when
// the target index is out of bounds for the set. `flat` is the linear
// pool index so the per-voxel mutation reuses the precomputed offset.
void applyEdit(
    IREntity::EntityId voxelSetEntity,
    C_VoxelSetNew &set,
    ivec3 localIdx,
    std::size_t flat,
    bool place,
    Color placeColor
) {
    UndoEdit edit{voxelSetEntity, localIdx, set.voxels_[flat]};
    g_editor.pendingStroke_.edits_.push_back(edit);
    if (place) {
        set.voxels_[flat].color_ = placeColor;
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
    set.syncActiveMask();
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
        IRPrefab::Voxel::recomputeFaceOccupancy(set.voxels_, set.size_);
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
        set.syncActiveMask();
        IRPrefab::Voxel::recomputeFaceOccupancy(set.voxels_, set.size_);
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
    Color color
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
            applyEdit(entity, set, local, flat, place, color);
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
    Color color
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
            applyEdit(entity, set, local, flat, place, color);
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
    Color color
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
        applyEdit(entity, set, cur, static_cast<std::size_t>(flat), place, color);
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

} // namespace

} // namespace IRVoxelEditor

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &IRVoxelEditor::g_autoWarmupFrames);
    IR_LOG_INFO("Starting creation: voxel_editor");
    IR_LOG_INFO("  Left-drag: AABB box-fill between drag-start and drag-end");
    IR_LOG_INFO("  Shift + left-drag: line-fill along dominant axis");
    IR_LOG_INFO("  Ctrl + left-click: face-fill (flood-fill axis-plane of hit face)");
    IR_LOG_INFO("  Left-click (no drag): place single voxel adjacent to hit face");
    IR_LOG_INFO("  Right-click: erase hit voxel (drag still rotates camera)");
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
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    using IRVoxelEditor::RotateParams;
    using IRVoxelEditor::ScrollZoomParams;

    auto scrollParams = std::make_unique<ScrollZoomParams>();
    auto *sp = scrollParams.get();
    auto scrollZoomSystem = IRSystem::createSystem<C_MouseScroll>(
        "EditorScrollZoom",
        [sp](C_MouseScroll &scroll) {
            if (scroll.yoffset_ > 0.0)
                ++sp->scrollDelta_;
            else if (scroll.yoffset_ < 0.0)
                --sp->scrollDelta_;
        },
        [sp]() { sp->cameraEntity_ = IREntity::getEntity("camera"); },
        [sp]() {
            if (sp->scrollDelta_ != 0) {
                auto &zoom = IREntity::getComponent<C_ZoomLevel>(sp->cameraEntity_);
                for (int i = 0; i < sp->scrollDelta_; ++i)
                    zoom.zoomIn();
                for (int i = 0; i > sp->scrollDelta_; --i)
                    zoom.zoomOut();
                sp->scrollDelta_ = 0;
            }
        }
    );
    IRSystem::setSystemParams(scrollZoomSystem, std::move(scrollParams));

    // Loft-mask render: draws the XZ and YZ mask grids onto the GUI canvas.
    // Runs in the RENDER pipeline after TEXT_TO_TRIXEL (canvas clear) so
    // the grids paint over the cleared canvas. Pixel-packing + texture
    // upload live in IRRender::drawMaskGridOntoCanvas (mask_grid_painter.hpp);
    // scratch_ is grown to the largest grid size and reused across frames.
    struct LoftRenderParams {
        C_TriangleCanvasTextures *canvas_ = nullptr;
        IRRender::MaskGridPaintScratch scratch_;
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
            const int sx = IRVoxelEditor::kEditableSceneSize.x;
            const int sy = IRVoxelEditor::kEditableSceneSize.y;
            const int sz = IRVoxelEditor::kEditableSceneSize.z;
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
            IRRender::renderText(
                *lrp->canvas_,
                "XZ",
                IRVoxelEditor::kLoftGridXZPos + ivec2(0, -12),
                Color{200, 220, 200, 220}
            );
            IRRender::renderText(
                *lrp->canvas_,
                "YZ",
                IRVoxelEditor::kLoftGridYZPos + ivec2(0, -12),
                Color{200, 220, 200, 220}
            );
            IRRender::renderText(
                *lrp->canvas_,
                "LOFT  Shift=sym  C=clear  Enter=stamp  F=exit",
                ivec2(IRVoxelEditor::kLoftGridXZPos.x, IRVoxelEditor::kLoftGridXZPos.y + gridH + 4),
                Color{200, 200, 200, 180}
            );
        }
    );
    IRSystem::setSystemParams(loftRenderSystem, std::move(loftRenderData));

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
            const int sx = IRVoxelEditor::kEditableSceneSize.x;
            const int sy = IRVoxelEditor::kEditableSceneSize.y;
            const int sz = IRVoxelEditor::kEditableSceneSize.z;
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
            // Resolve the swatch the user clicked this frame, if any.
            const int n = static_cast<int>(IRVoxelEditor::g_editor.paletteSwatches_.size());
            for (int i = 0; i < n; ++i) {
                if (IRPrefab::Widget::wasClicked(IRVoxelEditor::g_editor.paletteSwatches_[i])) {
                    IRVoxelEditor::g_editor.activeSwatchIdx_ = i;
                    break;
                }
            }
            // Always reconcile the selected-bit so the renderer reflects
            // whatever the active index is now (cheap: 16 boolean writes).
            for (int i = 0; i < n; ++i) {
                IRPrefab::Widget::setColorSwatchSelected(
                    IRVoxelEditor::g_editor.paletteSwatches_[i],
                    i == IRVoxelEditor::g_editor.activeSwatchIdx_
                );
            }
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
                const bool shiftNow = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift, 0u);
                const bool ctrlNow = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u);
                std::string status = ctrlNow ? "FACE" : (shiftNow ? "LINE" : "BOX");
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
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_bakePanel);
            if (!overWidget) {
                const int n = static_cast<int>(IRVoxelEditor::g_editor.paletteSwatches_.size());
                for (int i = 0; i < n; ++i) {
                    if (IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.paletteSwatches_[i])) {
                        overWidget = true;
                        break;
                    }
                }
            }

            const Color placeColor =
                IRVoxelEditor::kPaletteColors[IRVoxelEditor::g_editor.activeSwatchIdx_];

            // Right-click: single-voxel erase (unchanged).
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
                    IRVoxelEditor::applyFillFace(
                        hit->entity_,
                        set,
                        gpos,
                        hit->voxelPos_ + hit->faceNormal_,
                        hit->faceNormal_,
                        true,
                        placeColor
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
                    const ivec3 startPos = hit->voxelPos_ + hit->faceNormal_;
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
                    const ivec3 endPos = hit->voxelPos_ + hit->faceNormal_;
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

                if (startPos == endPos) {
                    ivec3 local{};
                    std::size_t flat = 0;
                    if (IRVoxelEditor::worldVoxelToLocal(set, gpos, startPos, local, flat))
                        IRVoxelEditor::applyEdit(targetEntity, set, local, flat, true, placeColor);
                } else if (shiftHeld) {
                    IRVoxelEditor::applyFillLine(
                        targetEntity,
                        set,
                        gpos,
                        startPos,
                        endPos,
                        true,
                        placeColor
                    );
                } else {
                    IRVoxelEditor::applyFillAABB(
                        targetEntity,
                        set,
                        gpos,
                        startPos,
                        endPos,
                        true,
                        placeColor
                    );
                }
                IRVoxelEditor::commitStroke();
            }
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GIZMO_SCREEN_SPACE_SIZE>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
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
    auto rotateSystem = IRSystem::createSystem<C_CameraYaw>(
        "EditorViewportRotate",
        [](C_CameraYaw &) {},
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
    // in beginTick over C_CameraYaw (the singleton camera entity), so
    // the swap lands BEFORE this frame's voxel-to-trixel stages read
    // C_VoxelSetNew::voxels_. Use the camera archetype filter because
    // we need a one-shot per-frame fire regardless of voxel-set state;
    // the per-entity tick is a no-op, all work happens in beginTick.
    // tickPlayback returns the next frame index via out-param without
    // touching g_anim.activeFrame_ — that lets switchToFrame snapshot
    // the old active frame's voxels before swapping in the new one.
    auto animPlaybackSystem = IRSystem::createSystem<C_CameraYaw>(
        "EditorAnimPlayback",
        [](C_CameraYaw &) {},
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
                    for (auto &v : set.voxels_) {
                        if (v.layer_id_ != delId)
                            continue;
                        v.layer_id_ = 0;
                        if (defaultVisible)
                            v.activate();
                        else
                            v.deactivate();
                    }
                    set.syncActiveMask();
                    IRPrefab::Voxel::recomputeFaceOccupancy(set.voxels_, set.size_);
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

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
         IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_LIST>(),
         IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
         scrubberSystem,
         layerSyncSystem,
         loftInputSystem,
         bakeSystem,
         paletteUpdateSystem,
         placeEraseSystem,
         scrollZoomSystem,
         IRSystem::createSystem<IRSystem::GIZMO_HOVER>(),
         IRSystem::createSystem<IRSystem::GIZMO_DRAG>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        rotateSystem,
        animPlaybackSystem,
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
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
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_COLOR_SWATCH>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
    };

    if (IRVoxelEditor::g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = IRVoxelEditor::g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRVoxelEditor::kShots;
        cfg.numShots_ = sizeof(IRVoxelEditor::kShots) / sizeof(IRVoxelEditor::kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
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
            if (!IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u))
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
                IRVoxelEditor::kEditableSceneSize,
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

    // Ctrl+O — load scene from disk, replacing all frames and layer state.
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonO,
        []() {
            if (!IRInput::checkKeyMouseModifiers(IRInput::kModifierControl, 0u))
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

    // Editable voxel set — the place/erase target. Allocated empty
    // (default color, alpha=255 so cells are active at start) then
    // every voxel is deactivated so the user starts from an empty
    // scene. A single floor row stays activated as a "ground" for the
    // first click to land on. Architect D1: the size is a named
    // constant on the editor side, not hardcoded inline.
    g_editor.editableVoxelSet_ = IREntity::createEntity(
        C_LocalTransform{IRVoxelEditor::kEditableSceneOrigin},
        C_VoxelSetNew{IRVoxelEditor::kEditableSceneSize, Color{200, 200, 210, 255}}
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
    IRPrefab::Widget::makeLabel(ivec2(kPanelPos.x + 12, kPanelPos.y + 36), "CLICK A SWATCH");

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
    constexpr ivec2 kBakePanelPos{130, 342};
    constexpr ivec2 kBakePanelSize{120, 140};
    IRVoxelEditor::g_bakePanel = IRPrefab::Widget::makePanel(kBakePanelPos, kBakePanelSize, "BAKE");
    IREntity::setComponent(IRVoxelEditor::g_bakePanel, IRComponents::C_HitBox2DGui{kBakePanelSize});
    IRVoxelEditor::g_bakeShapeList = IRPrefab::Widget::makeList(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 18),
        ivec2(112, 66),
        {"BOX", "SPHERE", "CYLINDER", "TORUS", "CONE", "ELLIPSOID"},
        1,
        11
    );
    IRVoxelEditor::g_bakeParam1Slider = IRPrefab::Widget::makeSlider(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 88),
        ivec2(112, 14),
        "P1",
        0.5f,
        12.0f,
        8.0f
    );
    IRVoxelEditor::g_bakeParam2Slider = IRPrefab::Widget::makeSlider(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 106),
        ivec2(112, 14),
        "P2",
        0.5f,
        12.0f,
        3.0f
    );
    IRVoxelEditor::g_bakeButton = IRPrefab::Widget::makeButton(
        ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 124),
        ivec2(112, 12),
        "BAKE"
    );

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
}
