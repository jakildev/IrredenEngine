// Trixel-rendered editor dockspace demo (T-148, F-0.2).
//
// Composes a fixed 4-zone editor dockspace (left + center + right +
// bottom) using the layout API. Splitters are draggable; panels can be
// dragged by their title bars to swap positions. Demonstrates the
// serialize/deserialize round-trip in-memory at startup.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_math.hpp>

// Systems — input
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_input_splitter.hpp>
#include <irreden/render/systems/system_widget_input_panel_drag.hpp>

// Systems — render
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_layout_compute.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_splitter.hpp>
#include <irreden/render/systems/system_widget_render_dock_preview.hpp>

// Layout + widgets
#include <irreden/render/layout.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/render/widget_theme.hpp>

// Commands
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <list>
#include <string>

namespace IRDockspaceDemo {

// Zone size constants — no magic numbers in initEntities.
constexpr int kLeftW = 240;
constexpr int kRightW = 240;
constexpr int kBottomH = 160;

namespace {
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, IRMath::vec2(0.0f, 0.0f), "dockspace_idle"},
};
int g_autoWarmupFrames = 0;
} // namespace

} // namespace IRDockspaceDemo

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &IRDockspaceDemo::g_autoWarmupFrames);
    IR_LOG_INFO("Starting creation: ui_dockspace");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT_SPLITTER>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT_PANEL_DRAG>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::LAYOUT_COMPUTE>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_SPLITTER>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_DOCK_PREVIEW>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

    if (IRDockspaceDemo::g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = IRDockspaceDemo::g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRDockspaceDemo::kShots;
        cfg.numShots_ = sizeof(IRDockspaceDemo::kShots) / sizeof(IRDockspaceDemo::kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    using namespace IRPrefab::Layout;
    using IRMath::ivec2;

    // Canvas bounds — the dockspace fills the entire GUI canvas.
    IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
    const auto &canvasTex =
        IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    const ivec2 canvasSize = canvasTex.size_;

    using namespace IRDockspaceDemo;

    // Build layout tree.
    //
    //  ROW (root)
    //    LEAF "left"     — fixed kLeftW
    //    COLUMN
    //      ROW
    //        LEAF "center"  — fraction 1.0
    //        LEAF "right"   — fixed kRightW
    //      LEAF "bottom"    — fixed kBottomH

    int rootRow = makeRow(-1, {SizeMode::FRACTION, 1.0f}, "root");

    // Left panel
    IREntity::EntityId leftPanel =
        IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(kLeftW, canvasSize.y), "SCENE");
    makeLeaf(rootRow, {SizeMode::FIXED_PX, static_cast<float>(kLeftW), 80, 480}, leftPanel, "left");

    // Center + right column
    int centerRightCol = makeColumn(rootRow, {SizeMode::FRACTION, 1.0f}, "center_right_col");

    // Center + right row (top part of the column)
    int topRow = makeRow(
        centerRightCol,
        {SizeMode::FRACTION, 1.0f, kBottomH + kSplitterThickness, 32767},
        "top_row"
    );

    IREntity::EntityId centerPanel = IRPrefab::Widget::makePanel(
        ivec2(0, 0),
        ivec2(canvasSize.x - kLeftW - kRightW, canvasSize.y - kBottomH),
        "VIEWPORT"
    );
    makeLeaf(topRow, {SizeMode::FRACTION, 1.0f, 160, 32767}, centerPanel, "center");

    IREntity::EntityId rightPanel = IRPrefab::Widget::makePanel(
        ivec2(0, 0),
        ivec2(kRightW, canvasSize.y - kBottomH),
        "PROPERTIES"
    );
    makeLeaf(
        topRow,
        {SizeMode::FIXED_PX, static_cast<float>(kRightW), 80, 480},
        rightPanel,
        "right"
    );

    // Bottom panel
    IREntity::EntityId bottomPanel =
        IRPrefab::Widget::makePanel(ivec2(0, 0), ivec2(canvasSize.x - kLeftW, kBottomH), "CONSOLE");
    makeLeaf(
        centerRightCol,
        {SizeMode::FIXED_PX, static_cast<float>(kBottomH), 80, 400},
        bottomPanel,
        "bottom"
    );

    // Wire splitter entities.
    buildSplitters();

    // Set root bounds — the layout compute system reads these each frame.
    setRoot(rootRow, ivec2(0, 0), canvasSize);

    // Verify serialize/deserialize round-trip in-memory.
    const std::string json = serialize();
    const bool ok = deserialize(json);
    if (!ok) {
        IR_LOG_WARN("ui_dockspace: layout serialize/deserialize round-trip FAILED");
    } else {
        IR_LOG_INFO("ui_dockspace: layout serialize/deserialize round-trip OK");
    }
}
