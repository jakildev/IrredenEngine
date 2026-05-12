// Trixel-rendered UI widget primitives demo (T-145, F-0.1).
//
// Shows the five Phase 0 minimum widgets: panel, label, button, slider,
// checkbox. The render harness fires `--auto-screenshot` if requested so
// the demo doubles as a visual-regression canary for the widget
// framework.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_math.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>

#include <irreden/render/widgets.hpp>
#include <irreden/render/widget_theme.hpp>

// Commands
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <cstdio>
#include <list>
#include <string>

namespace IRWidgetsDemo {

// Widget entity ids — captured at init so the per-frame poll loop can
// read interaction state without a query.
IREntity::EntityId g_panel = IREntity::kNullEntity;
IREntity::EntityId g_titleLabel = IREntity::kNullEntity;
IREntity::EntityId g_statusLabel = IREntity::kNullEntity;
IREntity::EntityId g_buttonEnable = IREntity::kNullEntity;
IREntity::EntityId g_buttonReset = IREntity::kNullEntity;
IREntity::EntityId g_buttonDisabled = IREntity::kNullEntity;
IREntity::EntityId g_sliderVolume = IREntity::kNullEntity;
IREntity::EntityId g_sliderBrightness = IREntity::kNullEntity;
IREntity::EntityId g_checkboxMusic = IREntity::kNullEntity;
IREntity::EntityId g_checkboxWireframe = IREntity::kNullEntity;

int g_clickCount = 0;
int g_resetCount = 0;
bool g_panelEnabled = true;

namespace {
// Two simple shots — wide and a slight pan so the screenshot harness
// records the widget set in two camera framings. Camera position is
// irrelevant for the GUI canvas but the harness still varies it.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, IRMath::vec2(0.0f, 0.0f), "widgets_idle"},
    {1.0f, IRMath::vec2(0.0f, 0.0f), "widgets_after_settle"},
};

int g_autoWarmupFrames = 0;
} // namespace

} // namespace IRWidgetsDemo

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &IRWidgetsDemo::g_autoWarmupFrames);
    IR_LOG_INFO("Starting creation: ui_widgets");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    // INPUT pipeline — hover test then widget state machine then
    // per-kind apply followers, before any system that consumes the
    // result on the same frame.
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {
            IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
            IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
            IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
        }
    );

    // UPDATE pipeline — per-frame creation logic that polls widget
    // state and pushes status text into the status label.
    auto pollId = IRSystem::createSystem<IRComponents::C_GuiElement>(
        "WidgetsDemo_Poll",
        [](const IRComponents::C_GuiElement &) {},
        []() {
            if (IRPrefab::Widget::wasClicked(IRWidgetsDemo::g_buttonReset)) {
                ++IRWidgetsDemo::g_resetCount;
                IRPrefab::Widget::setSliderValue(IRWidgetsDemo::g_sliderVolume, 0.5f);
                IRPrefab::Widget::setSliderValue(IRWidgetsDemo::g_sliderBrightness, 0.75f);
                IRPrefab::Widget::setCheckboxState(IRWidgetsDemo::g_checkboxMusic, true);
                IRPrefab::Widget::setCheckboxState(IRWidgetsDemo::g_checkboxWireframe, false);
            }
            if (IRPrefab::Widget::wasClicked(IRWidgetsDemo::g_buttonEnable)) {
                IRWidgetsDemo::g_panelEnabled = !IRWidgetsDemo::g_panelEnabled;
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_buttonReset, !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_sliderVolume, !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_checkboxMusic, !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setButtonLabel(
                    IRWidgetsDemo::g_buttonEnable,
                    IRWidgetsDemo::g_panelEnabled ? "DISABLE" : "ENABLE"
                );
            }
            if (IRPrefab::Widget::wasClicked(IRWidgetsDemo::g_buttonDisabled)) {
                ++IRWidgetsDemo::g_clickCount; // never fires; button stays disabled
            }

            char buf[128];
            std::snprintf(
                buf,
                sizeof(buf),
                "RESETS %d  MUSIC %s  WIRE %s",
                IRWidgetsDemo::g_resetCount,
                IRPrefab::Widget::checkboxState(IRWidgetsDemo::g_checkboxMusic) ? "ON" : "OFF",
                IRPrefab::Widget::checkboxState(IRWidgetsDemo::g_checkboxWireframe) ? "ON" : "OFF"
            );
            IRPrefab::Widget::setLabelText(IRWidgetsDemo::g_statusLabel, buf);
        }
    );
    IRSystem::registerPipeline(IRTime::Events::UPDATE, {pollId});

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

    if (IRWidgetsDemo::g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = IRWidgetsDemo::g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRWidgetsDemo::kShots;
        cfg.numShots_ =
            sizeof(IRWidgetsDemo::kShots) / sizeof(IRWidgetsDemo::kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    using IRMath::ivec2;

    // Panel framing the widget grid. Position is rough trixel coords on
    // the GUI canvas (top-left origin); fits inside the default GUI
    // canvas at standard guiScale.
    const ivec2 panelPos(60, 60);
    const ivec2 panelSize(520, 380);
    IRWidgetsDemo::g_panel = IRPrefab::Widget::makePanel(panelPos, panelSize, "TRIXEL UI WIDGETS");

    // Static intro label inside the panel.
    IRWidgetsDemo::g_titleLabel = IRPrefab::Widget::makeLabel(
        ivec2(panelPos.x + 16, panelPos.y + 56), "PHASE 0 PRIMITIVE SET"
    );

    // Dynamic status label, written by the poll system each frame.
    IRWidgetsDemo::g_statusLabel = IRPrefab::Widget::makeLabel(
        ivec2(panelPos.x + 16, panelPos.y + 80), "RESETS 0  MUSIC ON  WIRE OFF"
    );

    // Three buttons in a row.
    const int btnRowY = panelPos.y + 116;
    const int btnW = 140;
    const int btnH = 36;
    const int btnGap = 16;
    IRWidgetsDemo::g_buttonEnable = IRPrefab::Widget::makeButton(
        ivec2(panelPos.x + 16, btnRowY), ivec2(btnW, btnH), "DISABLE"
    );
    IRWidgetsDemo::g_buttonReset = IRPrefab::Widget::makeButton(
        ivec2(panelPos.x + 16 + btnW + btnGap, btnRowY), ivec2(btnW, btnH), "RESET"
    );
    IRWidgetsDemo::g_buttonDisabled = IRPrefab::Widget::makeButton(
        ivec2(panelPos.x + 16 + 2 * (btnW + btnGap), btnRowY), ivec2(btnW, btnH), "DISABLED"
    );
    IRPrefab::Widget::setDisabled(IRWidgetsDemo::g_buttonDisabled, true);

    // Two sliders.
    const int slW = 460;
    const int slH = 40;
    IRWidgetsDemo::g_sliderVolume = IRPrefab::Widget::makeSlider(
        ivec2(panelPos.x + 16, panelPos.y + 180), ivec2(slW, slH), "VOLUME", 0.0f, 1.0f, 0.5f
    );
    IRWidgetsDemo::g_sliderBrightness = IRPrefab::Widget::makeSlider(
        ivec2(panelPos.x + 16, panelPos.y + 232), ivec2(slW, slH), "BRIGHTNESS",
        0.0f, 1.0f, 0.75f
    );

    // Two checkboxes.
    const int cbW = 200;
    const int cbH = 24;
    IRWidgetsDemo::g_checkboxMusic = IRPrefab::Widget::makeCheckbox(
        ivec2(panelPos.x + 16, panelPos.y + 292), ivec2(cbW, cbH), "MUSIC", true
    );
    IRWidgetsDemo::g_checkboxWireframe = IRPrefab::Widget::makeCheckbox(
        ivec2(panelPos.x + 16 + cbW + btnGap, panelPos.y + 292),
        ivec2(cbW, cbH), "WIREFRAME", false
    );
}
