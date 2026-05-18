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
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
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
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
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
#include <utility>
#include <vector>

// Symmetry modes (T-212)
#include "symmetry.hpp"

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
    for (auto it = rec.edits_.rbegin(); it != rec.edits_.rend(); ++it) {
        // editable set lives for the session — no teardown path
        auto &set = IREntity::getComponent<C_VoxelSetNew>(it->voxelSet_);
        const std::size_t flat =
            static_cast<std::size_t>(IRMath::index3DtoIndex1D(it->localIdx_, set.size_));
        if (flat < set.voxels_.size()) {
            set.voxels_[flat] = it->prev_;
        }
    }
}

// Computes the local index inside `set` for a world voxel position.
// Returns true and writes `outLocal`/`outFlat` if the target lies in
// the set's bounds; false otherwise.
bool worldVoxelToLocal(
    const C_VoxelSetNew &set,
    const C_PositionGlobal3D &globalPos,
    ivec3 worldVoxel,
    ivec3 &outLocal,
    std::size_t &outFlat
) {
    if (set.numVoxels_ <= 0 || set.voxels_.empty()) {
        return false;
    }
    const ivec3 origin = IRMath::roundVec3HalfUp(globalPos.pos_);
    outLocal = worldVoxel - origin;
    if (outLocal.x < 0 || outLocal.x >= set.size_.x || outLocal.y < 0 ||
        outLocal.y >= set.size_.y || outLocal.z < 0 || outLocal.z >= set.size_.z) {
        return false;
    }
    outFlat = static_cast<std::size_t>(IRMath::index3DtoIndex1D(outLocal, set.size_));
    return outFlat < set.voxels_.size();
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
    IR_LOG_INFO("  Left-click: place voxel adjacent to hit face");
    IR_LOG_INFO("  Right-click: erase hit voxel (drag still rotates camera)");
    IR_LOG_INFO("  Middle-drag: pan camera");
    IR_LOG_INFO("  Scroll: zoom in/out");
    IR_LOG_INFO("  Q/E: snap-rotate 90 deg CCW/CW");
    IR_LOG_INFO("  Space: re-center + reset yaw");
    IR_LOG_INFO("  Ctrl+Z: undo last stroke");
    IR_LOG_INFO("  X/Y/Z: toggle X/Y/Z mirror symmetry");
    IR_LOG_INFO("  Left/Right arrow: previous/next frame");
    IR_LOG_INFO("  P: play/pause  A: add blank frame  D: duplicate  Backspace: delete frame");
    IR_LOG_INFO("  L: toggle loop mode (LOOP / PING-PONG)");
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

    // Place / erase driver: reads left-click PRESSED for place and
    // right-click PRESSED for erase. Right-DRAG keeps rotating the
    // camera (EditorViewportRotate below) — the rotate gesture only
    // mutates state while HELD with mouse movement, so a press alone
    // doesn't move the camera. Both actions cast a single ray per
    // click and operate on the place-adjacent / hit voxel
    // respectively. Single-voxel-per-click v1; drag-paint deferred to
    // a follow-up. Runs in INPUT, after the input/hover/widget chain
    // so the GUI's `fireAction_` (e.g. clicking a swatch) reaches the
    // poller above ahead of this system seeing the same press — but
    // GUI clicks land on widget hitboxes (the swatch panel is in the
    // top-left), so a click over the scene fires this system without
    // affecting the palette and vice versa.
    auto placeEraseSystem = IRSystem::createSystem<C_GuiElement>(
        "EditorPlaceErase",
        [](const C_GuiElement &) {},
        []() {},
        []() {
            // Mouse is hovering over the palette panel (or any other
            // widget) — let widgets eat the click so place/erase
            // doesn't fire under a swatch. Widget hover state is
            // populated by HITBOX_MOUSE_TEST_GUI earlier in this
            // pipeline tick.
            // Panel hitbox covers the whole palette dock; check it first
            // so clicks on the background (title bar, label gap, padding)
            // are suppressed without iterating the swatch list.
            bool overWidget = IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.palettePanel_) ||
                              IRPrefab::Widget::isHovered(IRVoxelEditor::g_layerPanel);
            if (!overWidget) {
                const int n = static_cast<int>(IRVoxelEditor::g_editor.paletteSwatches_.size());
                for (int i = 0; i < n; ++i) {
                    if (IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.paletteSwatches_[i])) {
                        overWidget = true;
                        break;
                    }
                }
            }

            const bool placeClick =
                !overWidget &&
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
            const bool eraseClick =
                !overWidget &&
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::PRESSED);

            if (!placeClick && !eraseClick) {
                return;
            }

            const auto hit = IRPrefab::Picking::castVoxelRay();
            if (!hit) {
                return;
            }
            // The picker leaves `faceNormal_ == ivec3(0)` for shape
            // hits (cube-face direction is undefined on non-box
            // SDFs). Place/erase needs a voxel hit; bail on shape
            // hits.
            if (hit->faceNormal_ == ivec3(0)) {
                return;
            }

            auto &set = IREntity::getComponent<C_VoxelSetNew>(hit->entity_);
            auto &gpos = IREntity::getComponent<C_PositionGlobal3D>(hit->entity_);

            ivec3 worldVoxel = hit->voxelPos_;
            if (placeClick) {
                worldVoxel = hit->voxelPos_ + hit->faceNormal_;
            }

            ivec3 localIdx{};
            std::size_t flat = 0;
            if (!IRVoxelEditor::worldVoxelToLocal(set, gpos, worldVoxel, localIdx, flat)) {
                return;
            }

            // Single-click strokes: same-frame append + commit before
            // return. `pendingStroke_.edits_` is always entered empty
            // and pre-reserved (init seeds it; commitStroke clears +
            // reserves on every exit), so no per-click clear/reserve is
            // needed. Drag-paint would split this into press → begin /
            // held → append / release → commit and would need an
            // explicit begin to bound the reserve to the brush AABB.
            const Color placeColor =
                IRVoxelEditor::kPaletteColors[IRVoxelEditor::g_editor.activeSwatchIdx_];
            IRVoxelEditor::applyEdit(hit->entity_, set, localIdx, flat, placeClick, placeColor);
            IRVoxelEditor::commitStroke();
        }
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GIZMO_SCREEN_SPACE_SIZE>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );

    // INPUT pipeline. WIDGET_INPUT writes per-widget hover/press/fire
    // state; the palette poller reads that state to set the active
    // swatch; the place/erase system reads `isHovered` to suppress
    // scene clicks under the palette. Gizmo input lands after the
    // widget chain so an over-gizmo click doesn't trip the scene-
    // edit path either.
    auto rotParams = std::make_unique<RotateParams>();
    auto *rp = rotParams.get();
    auto rotateSystem = IRSystem::createSystem<C_CameraYaw>(
        "EditorViewportRotate",
        [](C_CameraYaw &) {},
        [rp]() {
            bool rightPressed =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::PRESSED);
            bool rightHeld =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::HELD);

            if (rightPressed) {
                rp->firstRotFrame_ = true;
            }

            if (rightHeld) {
                vec2 mouse = IRInput::getMousePositionScreen();
                if (!rp->firstRotFrame_) {
                    float deltaX = mouse.x - rp->prevMouseX_;
                    IRPrefab::Camera::rotateYaw(deltaX * IRVoxelEditor::kRotationSensitivity);
                }
                rp->prevMouseX_ = mouse.x;
                rp->firstRotFrame_ = false;
            }
        }
    );
    IRSystem::setSystemParams(rotateSystem, std::move(rotParams));

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
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
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
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LIST>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_COLOR_SWATCH>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>(),
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
    IRCommand::registerCameraCommands();

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
        C_Position3D{vec3(0.0f, 0.0f, kFloorZ)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(40.0f, 40.0f, 1.0f, 0.0f),
            Color{55, 60, 75, 255}
        }
    );

    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(16.0f, 0.5f, 0.5f, 0.0f),
            Color{180, 60, 60, 255}
        }
    );

    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(0.5f, 16.0f, 0.5f, 0.0f),
            Color{60, 180, 60, 255}
        }
    );

    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
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
        IREntity::getComponent<C_Position3D>(translateGizmo).pos_ = vec3(-12.0f, 12.0f, -3.0f);

        IREntity::EntityId rotateGizmo = IRPrefab::Gizmo::createRotateGizmo();
        IREntity::getComponent<C_Position3D>(rotateGizmo).pos_ = vec3(12.0f, 12.0f, -3.0f);

        IREntity::EntityId scaleGizmo = IRPrefab::Gizmo::createScaleGizmo();
        IREntity::getComponent<C_Position3D>(scaleGizmo).pos_ = vec3(-12.0f, -12.0f, -3.0f);

        IREntity::EntityId jointMarker = IRPrefab::Gizmo::createJointMarker();
        IREntity::getComponent<C_Position3D>(jointMarker).pos_ = vec3(8.0f, -12.0f, -3.0f);

        IREntity::EntityId bindPointMarker = IRPrefab::Gizmo::createBindPointMarker();
        IREntity::getComponent<C_Position3D>(bindPointMarker).pos_ = vec3(12.0f, -12.0f, -3.0f);

        IREntity::EntityId ikMarker = IRPrefab::Gizmo::createIKMarker();
        IREntity::getComponent<C_Position3D>(ikMarker).pos_ = vec3(16.0f, -12.0f, -3.0f);
    }

    // Editable voxel set — the place/erase target. Allocated empty
    // (default color, alpha=255 so cells are active at start) then
    // every voxel is deactivated so the user starts from an empty
    // scene. A single floor row stays activated as a "ground" for the
    // first click to land on. Architect D1: the size is a named
    // constant on the editor side, not hardcoded inline.
    g_editor.editableVoxelSet_ = IREntity::createEntity(
        C_Position3D{IRVoxelEditor::kEditableSceneOrigin},
        C_VoxelSetNew{IRVoxelEditor::kEditableSceneSize, Color{200, 200, 210, 255}}
    );
    IRVoxelEditor::g_sceneVoxelSetEntity = g_editor.editableVoxelSet_;
    {
        auto &set = IREntity::getComponent<C_VoxelSetNew>(g_editor.editableVoxelSet_);
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
        C_Position3D{vec3(-16.0f, 0.0f, -6.0f)},
        C_VoxelSetNew{ivec3(4, 4, 4), Color{120, 180, 240, 255}}
    );
    IREntity::createEntity(
        C_Position3D{vec3(16.0f, 0.0f, -6.0f)},
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
}
