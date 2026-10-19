// Trixel-rendered UI widget primitives demo.
//
// Shows the widget set: panel, label, button, slider, number field, checkbox,
// list, dropdown, radio, text input, scroll, and image, plus a missing-file
// image's placeholder. `--auto-screenshot` runs the GUI-test shot table, so one
// capture serves render-verify (the references and the image-widget probe
// metric) and gui-verify (the image and number-field assertions).

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
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_gui_canvas_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_list.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_radio.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_apply_number_field.hpp>
#include <irreden/render/systems/system_widget_apply_scroll.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_image.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_list.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_radio.hpp>
#include <irreden/render/systems/system_widget_render_text_input.hpp>
#include <irreden/render/systems/system_widget_render_number_field.hpp>
#include <irreden/render/systems/system_widget_render_scroll.hpp>

#include <irreden/render/gui_test_assertions.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/render/widget_theme.hpp>

// Commands
#include <irreden/common/command_suite_capture.hpp>

#include <cstdio>
#include <iterator>
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
IREntity::EntityId g_numberFieldVolume = IREntity::kNullEntity;
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

// Image widgets. The probe PNG's quadrants are 1-px patterns whose box-filter
// averages differ from any single source pixel, which is what
// scripts/render-image-widget-metric.py samples; its geometry defaults mirror
// kImagePos / kImageSize and the probe's layout. `--image-path` swaps the PNG
// for the metric's negative control.
constexpr const char *kImageProbePath = "data/ui_widgets/image_widget_probe.png";
constexpr const char *kImageMissingPath = "data/ui_widgets/no_such_image.png";
const IRMath::ivec2 kImagePos(32, 350);
const IRMath::ivec2 kImageSize(96, 64);
const IRMath::ivec2 kImageMissingPos(136, 350);
IREntity::EntityId g_image = IREntity::kNullEntity;
IREntity::EntityId g_imageMissing = IREntity::kNullEntity;
std::string g_imagePath = kImageProbePath;

int g_clickCount = 0;
int g_resetCount = 0;
int g_digitCommandCount = 0;
bool g_panelEnabled = true;

namespace {
constexpr IRMath::ivec2 kNumberFieldScreen(424, 176);

constexpr IRVideo::GuiInputEvent kFocusedEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButton9},
    {1, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButton9},
    {2, IRVideo::GuiInputEvent::Type::MOVE, kNumberFieldScreen},
    {3, IRVideo::GuiInputEvent::Type::PRESS, kNumberFieldScreen, {}, IRInput::kMouseButtonLeft},
    {4, IRVideo::GuiInputEvent::Type::RELEASE, kNumberFieldScreen, {}, IRInput::kMouseButtonLeft},
    {5, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonBackspace},
    {6, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonBackspace},
    {7, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonBackspace},
    {8, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonBackspace},
    {9, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButton9},
    {10, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButton9},
    {11, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButton9},
    {12, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButton9},
};

constexpr IRVideo::GuiInputEvent kCommittedEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonEnter},
    {1, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonEnter},
    {2, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButton9},
    {3, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButton9},
};

constexpr IRVideo::GuiInputEvent kCancelledEvents[] = {
    {0, IRVideo::GuiInputEvent::Type::MOVE, kNumberFieldScreen},
    {1, IRVideo::GuiInputEvent::Type::PRESS, kNumberFieldScreen, {}, IRInput::kMouseButtonLeft},
    {2, IRVideo::GuiInputEvent::Type::RELEASE, kNumberFieldScreen, {}, IRInput::kMouseButtonLeft},
    {3, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonBackspace},
    {4, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonBackspace},
    {5, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonBackspace},
    {6, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonBackspace},
    {7, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonMinus},
    {8, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonMinus},
    {9, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButton1},
    {10, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButton1},
    {11, IRVideo::GuiInputEvent::Type::PRESS, {}, {}, IRInput::kKeyButtonEscape},
    {12, IRVideo::GuiInputEvent::Type::RELEASE, {}, {}, IRInput::kKeyButtonEscape},
};

constexpr IRVideo::GuiTestShot kShots[] = {
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "widgets_idle"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "widgets_after_settle"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "widgets_followup"}, nullptr, 0},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "number_field_focused"},
     kFocusedEvents,
     static_cast<int>(std::size(kFocusedEvents))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "number_field_committed"},
     kCommittedEvents,
     static_cast<int>(std::size(kCommittedEvents))},
    {{1.0f, IRMath::vec2(0.0f), 0.0f, "number_field_cancelled"},
     kCancelledEvents,
     static_cast<int>(std::size(kCancelledEvents))},
};

struct ExpectedState {
    bool editing_;
    float value_;
    bool captured_;
    int commandCount_;
    bool checkSlider_;
};

constexpr ExpectedState kFocusedExpected{true, 12.0f, true, 1, false};
constexpr ExpectedState kCommittedExpected{false, 64.0f, false, 2, true};
constexpr ExpectedState kCancelledExpected{false, 64.0f, false, 2, true};

bool numberFieldState(const void *context, std::string &actual) {
    const auto &expected = *static_cast<const ExpectedState *>(context);
    const auto &field =
        IREntity::getComponent<IRComponents::C_WidgetNumberField>(g_numberFieldVolume);
    const bool captured = IRCommand::getCommandManager().isKeyboardCaptured();
    const float slider = IRPrefab::Widget::sliderValue(g_sliderVolume);
    actual = "editing=" + std::to_string(field.editing_) +
             " value=" + std::to_string(field.value_) + " captured=" + std::to_string(captured) +
             " commands=" + std::to_string(g_digitCommandCount) +
             " slider=" + std::to_string(slider);
    return field.editing_ == expected.editing_ && field.value_ == expected.value_ &&
           captured == expected.captured_ && g_digitCommandCount == expected.commandCount_ &&
           (!expected.checkSlider_ || slider == expected.value_);
}

const IRPrefab::GuiTest::Assertion kFocusedAssertions[] = {
    IRPrefab::GuiTest::predicate(&numberFieldState, &kFocusedExpected, "number_field_focused"),
};
const IRPrefab::GuiTest::Assertion kCommittedAssertions[] = {
    IRPrefab::GuiTest::predicate(&numberFieldState, &kCommittedExpected, "number_field_committed"),
};
const IRPrefab::GuiTest::Assertion kCancelledAssertions[] = {
    IRPrefab::GuiTest::predicate(&numberFieldState, &kCancelledExpected, "number_field_cancelled"),
};

bool imageLoaded(const void *, std::string &actual) {
    const std::string &error = IRPrefab::Widget::imageError(g_image);
    actual = error.empty() ? "loaded" : error;
    return IRPrefab::Widget::imageLoaded(g_image) && error.empty();
}

bool missingImageErrorNamesPath(const void *, std::string &actual) {
    const std::string &error = IRPrefab::Widget::imageError(g_imageMissing);
    actual = error.empty() ? "no-error" : error;
    return !IRPrefab::Widget::imageLoaded(g_imageMissing) &&
           error.find(kImageMissingPath) != std::string::npos;
}

// The placeholder is what WIDGET_RENDER_IMAGE writes: every texel of the
// missing image's array matches the hatch in the theme's disabled colours.
bool missingImageDrawsPlaceholder(const void *, std::string &actual) {
    const auto &image = IREntity::getComponent<IRComponents::C_WidgetImage>(g_imageMissing);
    const IRPrefab::Widget::WidgetTheme &theme = IRPrefab::Widget::defaultTheme();
    if (image.texelSize_ != kImageSize ||
        image.texels_.size() != static_cast<std::size_t>(kImageSize.x) * kImageSize.y) {
        actual = "texel-array-size-mismatch";
        return false;
    }
    int mismatches = 0;
    for (int y = 0; y < kImageSize.y; ++y) {
        for (int x = 0; x < kImageSize.x; ++x) {
            const bool line = (x + y) % IRPrefab::Widget::detail::kImageHatchPeriod <
                              IRPrefab::Widget::detail::kImageHatchWidth;
            const IRMath::Color expected = line ? theme.textDisabled_ : theme.backgroundDisabled_;
            const IRMath::Color got = image.texels_[static_cast<std::size_t>(y) * kImageSize.x + x];
            if (got.toPackedRGBA() != expected.toPackedRGBA()) {
                ++mismatches;
            }
        }
    }
    actual = "mismatched-texels=" + std::to_string(mismatches);
    return mismatches == 0;
}

const IRPrefab::GuiTest::Assertion kIdleAssertions[] = {
    IRPrefab::GuiTest::predicate(&imageLoaded, nullptr, "image_loaded"),
    IRPrefab::GuiTest::predicate(&missingImageErrorNamesPath, nullptr, "missing_error_names_path"),
    IRPrefab::GuiTest::predicate(&missingImageDrawsPlaceholder, nullptr, "missing_placeholder"),
};

IRPrefab::GuiTest::LatchState g_guiLatch;

void onGuiAssertFrame(int shotIndex, bool isCaptureFrame) {
    const IRPrefab::GuiTest::Assertion *assertions = nullptr;
    int count = 0;
    if (shotIndex == 0) {
        assertions = kIdleAssertions;
        count = static_cast<int>(std::size(kIdleAssertions));
    } else if (shotIndex == 3) {
        assertions = kFocusedAssertions;
        count = static_cast<int>(std::size(kFocusedAssertions));
    } else if (shotIndex == 4) {
        assertions = kCommittedAssertions;
        count = static_cast<int>(std::size(kCommittedAssertions));
    } else if (shotIndex == 5) {
        assertions = kCancelledAssertions;
        count = static_cast<int>(std::size(kCancelledAssertions));
    }
    IRPrefab::GuiTest::onFrame(
        g_guiLatch,
        shotIndex,
        isCaptureFrame,
        kShots[shotIndex].render_.label_,
        assertions,
        count
    );
}

int g_autoWarmupFrames = 0;
} // namespace

} // namespace IRWidgetsDemo

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: ui_widgets");
    IREngine::args().string(
        "--image-path",
        "PNG the image widget shows (run-dir relative)",
        IRWidgetsDemo::kImageProbePath
    );
    IREngine::init(argc, argv);
    IRWidgetsDemo::g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();
    IRWidgetsDemo::g_imagePath = IREngine::args().getString("--image-path");
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
            IRSystem::createSystem<IRSystem::WIDGET_APPLY_NUMBER_FIELD>(),
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
                IRPrefab::Widget::setSliderValue(IRWidgetsDemo::g_sliderVolume, 12.0f);
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
            if (IRPrefab::Widget::isPressed(IRWidgetsDemo::g_sliderVolume)) {
                IRPrefab::Widget::setNumberFieldValue(
                    IRWidgetsDemo::g_numberFieldVolume,
                    IRPrefab::Widget::sliderValue(IRWidgetsDemo::g_sliderVolume)
                );
            } else {
                IRPrefab::Widget::setSliderValue(
                    IRWidgetsDemo::g_sliderVolume,
                    IRPrefab::Widget::numberFieldValue(IRWidgetsDemo::g_numberFieldVolume)
                );
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
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {pollId, IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_IMAGE>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_LIST>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_RADIO>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_TEXT_INPUT>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_NUMBER_FIELD>(),
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_SCROLL>(),
            // Dropdown renders LAST among widgets so its open panel paints
            // over any neighbor it overlaps.
            IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::GUI_CANVAS_TO_SCREEN>(),
        }
    );

    if (IRWidgetsDemo::g_autoWarmupFrames > 0) {
        IRVideo::GuiTestConfig config{};
        config.warmupFrames_ = IRWidgetsDemo::g_autoWarmupFrames;
        config.settleFrames_ = 3;
        config.shots_ = IRWidgetsDemo::kShots;
        config.numShots_ = static_cast<int>(std::size(IRWidgetsDemo::kShots));
        config.onAssertFrame_ = &IRWidgetsDemo::onGuiAssertFrame;
        renderPipeline.push_back(IRVideo::createGuiTestSystem(config));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
    IRCommand::createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButton9, []() {
        ++IRWidgetsDemo::g_digitCommandCount;
    });
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
        ivec2(170, slH),
        "VOLUME",
        1.0f,
        64.0f,
        12.0f
    );
    IRWidgetsDemo::g_numberFieldVolume = IRPrefab::Widget::makeNumberField(
        ivec2(leftX + 178, panelPos.y + 130),
        ivec2(90, slH),
        12.0f,
        1.0f,
        64.0f,
        1.0f
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

    // ---- Image widgets (left column, below the Phase 0 set) --------------
    IRPrefab::Widget::makeLabel(IRWidgetsDemo::kImagePos - ivec2(0, 20), "IMAGE");
    IRPrefab::Widget::makeLabel(IRWidgetsDemo::kImageMissingPos - ivec2(0, 20), "MISSING");
    IRWidgetsDemo::g_image = IRPrefab::Widget::makeImage(
        IRWidgetsDemo::kImagePos,
        IRWidgetsDemo::kImageSize,
        IRWidgetsDemo::g_imagePath
    );
    IRWidgetsDemo::g_imageMissing = IRPrefab::Widget::makeImage(
        IRWidgetsDemo::kImageMissingPos,
        IRWidgetsDemo::kImageSize,
        IRWidgetsDemo::kImageMissingPath
    );
}
