#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/common/command_suite_registry.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using IRCommand::BindingOverrides;
using IRCommand::DefaultBinding;
using IRCommand::Suite;

// CommandManager stamps g_commandManager in its ctor; EntityManager is needed
// by any prefab command body that touches ECS state. Nothing here fires a
// command, so no render/video manager is required.
class DefaultBindingManifestTest : public testing::Test {
  protected:
    // "NAME: KEY" for every PRESSED registration, in registration order — the
    // same projection `buildCommandListText()` renders into the debug help
    // overlay, so an unchanged vector here IS unchanged overlay text.
    std::vector<std::string> pressedRegistrations() const {
        std::vector<std::string> rows;
        for (const auto &reg : IRCommand::getCommandManager().getCommandRegistrations()) {
            rows.push_back(
                IRCommand::modifierString(reg.requiredModifiers) +
                IRCommand::keyButtonToString(reg.button) + ": " + reg.name
            );
        }
        return rows;
    }

    // Total rows registered since the fixture was built, INCLUDING the
    // RELEASED `MOVE_CAMERA_*_END` rows that never reach the registration map.
    // `createCommand` returns `m_userCommands.size() - 1`, so a probe binding's
    // id is exactly the number of rows registered before it.
    int totalRowsRegistered() const {
        return static_cast<int>(IRCommand::createCommand(
            IRInput::KEY_MOUSE,
            IRInput::PRESSED,
            IRInput::kKeyButtonF24,
            []() {}
        ));
    }

    IREntity::EntityManager m_entity_manager;
    IRCommand::CommandManager m_command_manager;
};

// ---- Zero-arg suites are unchanged ----------------------------------------

TEST_F(DefaultBindingManifestTest, CameraSuiteZeroArgMatchesPreManifestRegistrations) {
    IRCommand::registerCameraCommands();

    // The engine's default camera keys, spelled out. Pinning the exact
    // sequence is the point: the manifest and the help overlay it feeds are
    // user-visible surface, so a reordering or dropped row must fail here.
    EXPECT_EQ(
        pressedRegistrations(),
        (std::vector<std::string>{
            "ESC: CLOSE WINDOW",
            "=: ZOOM IN",
            "-: ZOOM OUT",
            "W: CAMERA UP",
            "S: CAMERA DOWN",
            "A: CAMERA LEFT",
            "D: CAMERA RIGHT",
        })
    );
    // 11 total: the 7 above plus the 4 RELEASED MOVE_CAMERA_*_END rows.
    EXPECT_EQ(totalRowsRegistered(), 11);
}

TEST_F(DefaultBindingManifestTest, CaptureSuiteZeroArgMatchesPreManifestRegistrations) {
    IRCommand::registerCaptureCommands();

    // Pinning the exact rendered sequence, same as the camera suite above.
    // SCREENSHOT_CANVAS used to render as "UNKNOWN" here — it had no
    // `commandNameToString` case, and this test asserted that deliberately
    // rather than papering over it. #2550 replaced that hand-listed switch
    // with the `kCommandInfo` catalog, which gives every enum value a real
    // label by static_assert, so the label is now the fixed one.
    EXPECT_EQ(
        pressedRegistrations(),
        (std::vector<std::string>{
            "F8: SCREENSHOT",
            "F7: SCREENSHOT CANVAS",
            "F9: RECORD TOGGLE",
        })
    );
    EXPECT_EQ(totalRowsRegistered(), 3);
}

// ---- Enumeration ----------------------------------------------------------

TEST_F(DefaultBindingManifestTest, SuiteDefaultsReportsEveryRowIncludingReleased) {
    const auto camera = IRCommand::suiteDefaults(Suite::CAMERA);
    ASSERT_EQ(camera.size(), 11u);

    int releasedRows = 0;
    for (const DefaultBinding &binding : camera) {
        if (binding.status_ == IRInput::RELEASED) {
            ++releasedRows;
        }
    }
    // The four MOVE_CAMERA_*_END rows — invisible to getCommandRegistrations(),
    // which is why the manifest and not the registration map is the source of
    // truth for enumeration.
    EXPECT_EQ(releasedRows, 4);

    EXPECT_EQ(IRCommand::suiteDefaults(Suite::CAPTURE).size(), 3u);
}

TEST_F(DefaultBindingManifestTest, CameraPanPairsShareOneButtonPerAxis) {
    // The property that makes a single remap entry carry a pan axis's PRESSED
    // and RELEASED rows together (see the remap test below).
    for (const int button :
         {IRInput::kKeyButtonW, IRInput::kKeyButtonA, IRInput::kKeyButtonS, IRInput::kKeyButtonD}) {
        int rowsOnButton = 0;
        for (const DefaultBinding &binding : IRCommand::suiteDefaults(Suite::CAMERA)) {
            if (binding.button_ == button) {
                ++rowsOnButton;
            }
        }
        EXPECT_EQ(rowsOnButton, 2) << "button " << IRCommand::keyButtonToString(button);
    }
}

// ---- Omit -----------------------------------------------------------------

TEST_F(DefaultBindingManifestTest, OmitDropsOnlyTheNamedCommand) {
    IRCommand::registerCameraCommands({.omit_ = {IRCommand::CLOSE_WINDOW}});

    // Escape is gone; everything else binds, in the same order — the
    // voxel_editor migration's exact shape.
    EXPECT_EQ(
        pressedRegistrations(),
        (std::vector<std::string>{
            "=: ZOOM IN",
            "-: ZOOM OUT",
            "W: CAMERA UP",
            "S: CAMERA DOWN",
            "A: CAMERA LEFT",
            "D: CAMERA RIGHT",
        })
    );
    EXPECT_EQ(totalRowsRegistered(), 10);
}

TEST_F(DefaultBindingManifestTest, OmitOfAPressedHalfLeavesTheReleasedRowBound) {
    // The pan pair is two DISTINCT commands, so omitting the START half does
    // not take the END half with it. Documented in BindingOverrides; asserted
    // here so the semantics can't drift silently.
    IRCommand::registerCameraCommands({.omit_ = {IRCommand::MOVE_CAMERA_UP_START}});

    const auto rows = pressedRegistrations();
    EXPECT_EQ(std::ranges::count(rows, std::string{"W: CAMERA UP"}), 0);
    EXPECT_EQ(totalRowsRegistered(), 10);
}

// ---- Remap ----------------------------------------------------------------

TEST_F(DefaultBindingManifestTest, RemapMovesWasdOntoArrowsAndLeavesNoWasdRows) {
    IRCommand::registerCameraCommands(
        {.remap_ = {
             {IRInput::kKeyButtonW, IRInput::kKeyButtonUp},
             {IRInput::kKeyButtonA, IRInput::kKeyButtonLeft},
             {IRInput::kKeyButtonS, IRInput::kKeyButtonDown},
             {IRInput::kKeyButtonD, IRInput::kKeyButtonRight},
         }}
    );

    EXPECT_EQ(
        pressedRegistrations(),
        (std::vector<std::string>{
            "ESC: CLOSE WINDOW",
            "=: ZOOM IN",
            "-: ZOOM OUT",
            "UP: CAMERA UP",
            "DOWN: CAMERA DOWN",
            "LEFT: CAMERA LEFT",
            "RIGHT: CAMERA RIGHT",
        })
    );
    // Row count is unchanged — a remap moves rows, it never drops them.
    EXPECT_EQ(totalRowsRegistered(), 11);
}

TEST_F(DefaultBindingManifestTest, RemapMovesEveryRowSharingTheButton) {
    // Why one `{W, UP}` entry carries both the PRESSED and RELEASED pan rows:
    // remap matches on the button, so every row using it moves. Spelled with
    // two PRESSED rows here so both are observable in the registration map.
    static constexpr DefaultBinding kTwoOnOneButton[] = {
        {IRCommand::ZOOM_IN, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonW},
        {IRCommand::ZOOM_OUT, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonW},
    };
    IRCommand::registerBindings(
        kTwoOnOneButton,
        {.remap_ = {{IRInput::kKeyButtonW, IRInput::kKeyButtonUp}}}
    );

    EXPECT_EQ(pressedRegistrations(), (std::vector<std::string>{"UP: ZOOM IN", "UP: ZOOM OUT"}));
}

TEST_F(DefaultBindingManifestTest, RemapDoesNotChain) {
    // First matching pair wins per row, so {W->A, A->UP} leaves W on A rather
    // than carrying it through to UP.
    static constexpr DefaultBinding kOneRow[] = {
        {IRCommand::ZOOM_IN, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonW},
    };
    IRCommand::registerBindings(
        kOneRow,
        {.remap_ = {
             {IRInput::kKeyButtonW, IRInput::kKeyButtonA},
             {IRInput::kKeyButtonA, IRInput::kKeyButtonUp},
         }}
    );

    EXPECT_EQ(pressedRegistrations(), (std::vector<std::string>{"A: ZOOM IN"}));
}

// ---- Generality: the mechanism is not suite-specific -----------------------

TEST_F(DefaultBindingManifestTest, CallerAuthoredManifestHonorsOmitAndRemap) {
    // A creation's own table, not one of the two engine suites, through the
    // same primitive with the same override semantics.
    static constexpr DefaultBinding kCreationBindings[] = {
        {IRCommand::TOGGLE_GUI, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonG},
        {IRCommand::GUI_ZOOM_IN, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonEqual},
        {IRCommand::GUI_ZOOM_OUT, IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonMinus},
    };
    IRCommand::registerBindings(
        kCreationBindings,
        {.omit_ = {IRCommand::GUI_ZOOM_OUT},
         .remap_ = {{IRInput::kKeyButtonG, IRInput::kKeyButtonF1}}}
    );

    EXPECT_EQ(
        pressedRegistrations(),
        (std::vector<std::string>{"F1: TOGGLE GUI", "=: GUI ZOOM IN"})
    );
}

TEST_F(DefaultBindingManifestTest, RequiredModifiersSurviveRegistration) {
    static constexpr DefaultBinding kModifierBinding[] = {
        {IRCommand::TOGGLE_GUI,
         IRInput::KEY_MOUSE,
         IRInput::PRESSED,
         IRInput::kKeyButtonG,
         IRInput::kModifierShift | IRInput::kModifierControl},
    };
    IRCommand::registerBindings(kModifierBinding);

    EXPECT_EQ(pressedRegistrations(), (std::vector<std::string>{"SHIFT+CTRL+G: TOGGLE GUI"}));
}

TEST_F(DefaultBindingManifestTest, EmptyOverridesRegisterTheWholeManifest) {
    IRCommand::registerBindings(IRCommand::suiteDefaults(Suite::CAPTURE), {});
    EXPECT_EQ(totalRowsRegistered(), 3);
}

} // namespace
