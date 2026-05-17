#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>
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

// Camera prefab namespace (Z-yaw API)
#include <irreden/render/camera.hpp>

// Command suites
#include <irreden/common/command_suite_camera.hpp>

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
// is enough headroom for a 1024-edit stroke (24 KiB) to live ~40
// strokes deep before the oldest evicts.
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
};

EditorState g_editor;

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
        set.voxels_[flat].activate();
    } else {
        set.voxels_[flat].deactivate();
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
            bool overWidget =
                IRPrefab::Widget::isHovered(IRVoxelEditor::g_editor.palettePanel_);
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

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
         IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
         paletteUpdateSystem,
         placeEraseSystem,
         scrollZoomSystem,
         IRSystem::createSystem<IRSystem::GIZMO_HOVER>(),
         IRSystem::createSystem<IRSystem::GIZMO_DRAG>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        rotateSystem,
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
        IR_LOG_INFO("Symmetry: X=%s Y=%s Z=%s",
            IRVoxelEditor::g_symmetry.enableX_ ? "ON" : "OFF",
            IRVoxelEditor::g_symmetry.enableY_ ? "ON" : "OFF",
            IRVoxelEditor::g_symmetry.enableZ_ ? "ON" : "OFF");
    };
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonX,
        [logSymmetry]() { IRVoxelEditor::g_symmetry.enableX_ = !IRVoxelEditor::g_symmetry.enableX_; logSymmetry(); }
    );
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonY,
        [logSymmetry]() { IRVoxelEditor::g_symmetry.enableY_ = !IRVoxelEditor::g_symmetry.enableY_; logSymmetry(); }
    );
    // Ctrl+Z — undo the most recent stroke. The modifier check happens
    // inline because IRCommand bindings don't take modifier masks; a
    // bare Z press without Ctrl is intentionally ignored (Z alone may
    // become a different editor hotkey later).
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
        [logSymmetry]() { IRVoxelEditor::g_symmetry.enableZ_ = !IRVoxelEditor::g_symmetry.enableZ_; logSymmetry(); }
    );
}

void initEntities() {
    using IRVoxelEditor::g_editor;

    // Pre-size the in-flight stroke buffer so a click's append doesn't
    // pay a first-time allocation cost.
    g_editor.pendingStroke_.edits_.reserve(IRVoxelEditor::kUndoStrokeReserve);

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
}
