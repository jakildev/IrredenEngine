// Trixel-rendered UI widget primitives demo (T-145 + T-177, F-0.1).
//
// Shows the ten Phase 0 / Phase 0 follow-up widgets: panel, label, button,
// slider, checkbox, list, dropdown, radio, text input, scroll. The render
// harness fires `--auto-screenshot` if requested so the demo doubles as a
// visual-regression canary for the widget framework.

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
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_radio.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_apply_scroll.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_radio.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/render/systems/system_widget_render_scroll.hpp>

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

// T-177 follow-up widgets.
IREntity::EntityId g_list = IREntity::kNullEntity;
IREntity::EntityId g_dropdown = IREntity::kNullEntity;
IREntity::EntityId g_radioLow = IREntity::kNullEntity;
IREntity::EntityId g_radioMed = IREntity::kNullEntity;
IREntity::EntityId g_radioHigh = IREntity::kNullEntity;
IREntity::EntityId g_textInput = IREntity::kNullEntity;
IREntity::EntityId g_scroll = IREntity::kNullEntity;

int g_clickCount = 0;
int g_resetCount = 0;
bool g_panelEnabled = true;

namespace {
// Three shots — wide framings so the screenshot harness records the
// full widget set. Camera position is irrelevant for the GUI canvas but
// the harness still varies it.
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, IRMath::vec2(0.0f, 0.0f), "widgets_idle"},
    {1.0f, IRMath::vec2(0.0f, 0.0f), "widgets_after_settle"},
    {1.0f, IRMath::vec2(0.0f, 0.0f), "widgets_followup"},
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
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_LIST>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_RADIO>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_SCROLL>(),
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
                IRPrefab::Widget::setListSelectedIndex(IRWidgetsDemo::g_list, 0);
                IRPrefab::Widget::setDropdownSelectedIndex(IRWidgetsDemo::g_dropdown, 0);
                IRPrefab::Widget::setTextInputValue(IRWidgetsDemo::g_textInput, "HELLO");
                IRPrefab::Widget::setScrollPosition(IRWidgetsDemo::g_scroll, 0);
            }
            if (IRPrefab::Widget::wasClicked(IRWidgetsDemo::g_buttonEnable)) {
                IRWidgetsDemo::g_panelEnabled = !IRWidgetsDemo::g_panelEnabled;
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_buttonReset,
                    !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_sliderVolume,
                    !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setDisabled(
                    IRWidgetsDemo::g_checkboxMusic,
                    !IRWidgetsDemo::g_panelEnabled
                );
                IRPrefab::Widget::setButtonLabel(
                    IRWidgetsDemo::g_buttonEnable,
                    IRWidgetsDemo::g_panelEnabled ? "DISABLE" : "ENABLE"
                );
            }
            if (IRPrefab::Widget::wasClicked(IRWidgetsDemo::g_buttonDisabled)) {
                ++IRWidgetsDemo::g_clickCount; // never fires; button stays disabled
            }

            char buf[160];
            const int quality = IRPrefab::Widget::radioSelected(IRWidgetsDemo::g_radioHigh)  ? 2
                                : IRPrefab::Widget::radioSelected(IRWidgetsDemo::g_radioMed) ? 1
                                                                                             : 0;
            std::snprintf(
                buf,
                sizeof(buf),
                "RESETS %d  LIST %d  DD %d  Q %d  TXT %s",
                IRWidgetsDemo::g_resetCount,
                IRPrefab::Widget::listSelectedIndex(IRWidgetsDemo::g_list),
                IRPrefab::Widget::dropdownSelectedIndex(IRWidgetsDemo::g_dropdown),
                quality,
                IRPrefab::Widget::textInputValue(IRWidgetsDemo::g_textInput).c_str()
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
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LIST>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_RADIO>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_TEXT_INPUT>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_SCROLL>(),
        // Dropdown renders LAST among widgets so its open panel paints
        // over any neighbor it overlaps.
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

    if (IRWidgetsDemo::g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = IRWidgetsDemo::g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = IRWidgetsDemo::kShots;
        cfg.numShots_ = sizeof(IRWidgetsDemo::kShots) / sizeof(IRWidgetsDemo::kShots[0]);
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

    // The GUI canvas with the default game resolution (1280x720) at
    // gui_scale=1 measures 640x720 trixels — the panel must fit inside
    // that. Two-column layout: Phase 0 widgets on the left, T-177
    // follow-ups on the right.
    const ivec2 panelPos(20, 30);
    const ivec2 panelSize(600, 660);
    IRWidgetsDemo::g_panel = IRPrefab::Widget::makePanel(panelPos, panelSize, "TRIXEL UI WIDGETS");

    IRWidgetsDemo::g_titleLabel = IRPrefab::Widget::makeLabel(
        ivec2(panelPos.x + 12, panelPos.y + 36),
        "PHASE 0 + T-177 PRIMITIVE SET"
    );
    IRWidgetsDemo::g_statusLabel = IRPrefab::Widget::makeLabel(
        ivec2(panelPos.x + 12, panelPos.y + 56),
        "RESETS 0  LIST -1  DD 0  Q 0  TXT HELLO"
    );

    // ---- Left column (Phase 0 widgets) -----------------------------------
    const int leftX = panelPos.x + 12;

    const int btnRowY = panelPos.y + 88;
    const int btnW = 86;
    const int btnH = 26;
    const int btnGap = 6;
    IRWidgetsDemo::g_buttonEnable =
        IRPrefab::Widget::makeButton(ivec2(leftX, btnRowY), ivec2(btnW, btnH), "DISABLE");
    IRWidgetsDemo::g_buttonReset = IRPrefab::Widget::makeButton(
        ivec2(leftX + btnW + btnGap, btnRowY),
        ivec2(btnW, btnH),
        "RESET"
    );
    IRWidgetsDemo::g_buttonDisabled = IRPrefab::Widget::makeButton(
        ivec2(leftX + 2 * (btnW + btnGap), btnRowY),
        ivec2(btnW, btnH),
        "DISABLED"
    );
    IRPrefab::Widget::setDisabled(IRWidgetsDemo::g_buttonDisabled, true);

    const int slW = 268;
    const int slH = 30;
    IRWidgetsDemo::g_sliderVolume = IRPrefab::Widget::makeSlider(
        ivec2(leftX, panelPos.y + 130),
        ivec2(slW, slH),
        "VOLUME",
        0.0f,
        1.0f,
        0.5f
    );
    IRWidgetsDemo::g_sliderBrightness = IRPrefab::Widget::makeSlider(
        ivec2(leftX, panelPos.y + 170),
        ivec2(slW, slH),
        "BRIGHTNESS",
        0.0f,
        1.0f,
        0.75f
    );

    const int cbW = 128;
    const int cbH = 22;
    IRWidgetsDemo::g_checkboxMusic = IRPrefab::Widget::makeCheckbox(
        ivec2(leftX, panelPos.y + 216),
        ivec2(cbW, cbH),
        "MUSIC",
        true
    );
    IRWidgetsDemo::g_checkboxWireframe = IRPrefab::Widget::makeCheckbox(
        ivec2(leftX + cbW + btnGap, panelPos.y + 216),
        ivec2(cbW, cbH),
        "WIREFRAME",
        false
    );

    // ---- Right column (T-177 follow-up widgets) --------------------------
    const int rightX = panelPos.x + 296;
    const int rightW = 290;

    // List of selectable items.
    IRWidgetsDemo::g_list = IRPrefab::Widget::makeList(
        ivec2(rightX, panelPos.y + 88),
        ivec2(140, 110),
        std::vector<std::string>{"RED", "GREEN", "BLUE", "CYAN", "MAGENTA", "YELLOW"},
        0,
        18
    );

    // Dropdown — sits to the right of the list.
    IRWidgetsDemo::g_dropdown = IRPrefab::Widget::makeDropdown(
        ivec2(rightX + 148, panelPos.y + 88),
        ivec2(140, 26),
        std::vector<std::string>{"EASY", "NORMAL", "HARD", "EXTREME"},
        1,
        18
    );

    // Radio group (quality: low / med / high), stacked below the dropdown.
    const std::uint32_t qualityGroup = 0x71u;
    IRWidgetsDemo::g_radioLow = IRPrefab::Widget::makeRadio(
        ivec2(rightX + 148, panelPos.y + 124),
        ivec2(140, 22),
        "LOW",
        qualityGroup,
        0,
        /*initialSelected*/ false
    );
    IRWidgetsDemo::g_radioMed = IRPrefab::Widget::makeRadio(
        ivec2(rightX + 148, panelPos.y + 150),
        ivec2(140, 22),
        "MED",
        qualityGroup,
        1,
        /*initialSelected*/ true
    );
    IRWidgetsDemo::g_radioHigh = IRPrefab::Widget::makeRadio(
        ivec2(rightX + 148, panelPos.y + 176),
        ivec2(140, 22),
        "HIGH",
        qualityGroup,
        2,
        /*initialSelected*/ false
    );

    // Text input — spans the right column.
    IRWidgetsDemo::g_textInput = IRPrefab::Widget::makeTextInput(
        ivec2(rightX, panelPos.y + 216),
        ivec2(rightW - 4, 28),
        "HELLO",
        24
    );

    // Horizontal scroll bar — visual + draggable thumb. Demonstrates the
    // axis switch and a non-zero initial scroll position.
    IRWidgetsDemo::g_scroll = IRPrefab::Widget::makeScroll(
        ivec2(rightX, panelPos.y + 256),
        ivec2(rightW - 4, 24),
        /*contentSize*/ 800,
        IRComponents::C_WidgetScroll::Axis::HORIZONTAL,
        /*initialScroll*/ 200
    );
}
