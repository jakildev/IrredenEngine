#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/command/action_registry.hpp>
#include <irreden/command/command_manager.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// Runtime action assignment, driven through the production keyboard dispatcher
// (`executeUserKeyboardCommands`) from a deterministic button snapshot. Each
// test proves a binding change through both halves the issue names: dispatch
// (what a key now fires) and introspection (what the registry and the help
// overlay's registration list report).

namespace {

using IRCommand::ActionSpec;
using IRCommand::BindStatus;
using IRCommand::ConflictPolicy;
using IRCommand::KeyChord;
using IRInput::ButtonStatuses;
using IRInput::KeyModifierMask;
using IRInput::KeyMouseButtons;

class SnapshotInput {
  public:
    SnapshotInput() {
        m_states.fill(ButtonStatuses::NOT_HELD);
    }

    void press(KeyMouseButtons button) {
        m_states[static_cast<std::size_t>(button)] = ButtonStatuses::PRESSED;
    }

    void release(KeyMouseButtons button) {
        ButtonStatuses &state = m_states[static_cast<std::size_t>(button)];
        state = state == ButtonStatuses::PRESSED ? ButtonStatuses::PRESSED_AND_RELEASED
                                                 : ButtonStatuses::RELEASED;
    }

    void advance() {
        for (ButtonStatuses &state : m_states) {
            if (state == ButtonStatuses::PRESSED) {
                state = ButtonStatuses::HELD;
            } else if (
                state == ButtonStatuses::RELEASED || state == ButtonStatuses::PRESSED_AND_RELEASED
            ) {
                state = ButtonStatuses::NOT_HELD;
            }
        }
    }

    bool checkButton(KeyMouseButtons button, ButtonStatuses status) const {
        const ButtonStatuses state = m_states[static_cast<std::size_t>(button)];
        if (status == ButtonStatuses::PRESSED) {
            return state == ButtonStatuses::PRESSED ||
                   state == ButtonStatuses::PRESSED_AND_RELEASED;
        }
        if (status == ButtonStatuses::RELEASED) {
            return state == ButtonStatuses::RELEASED ||
                   state == ButtonStatuses::PRESSED_AND_RELEASED;
        }
        if (status == ButtonStatuses::HELD) {
            return state == ButtonStatuses::PRESSED || state == ButtonStatuses::HELD;
        }
        return false;
    }

    KeyModifierMask heldModifiers() const {
        KeyModifierMask held = IRInput::kModifierNone;
        if (checkButton(IRInput::kKeyButtonLeftShift, ButtonStatuses::HELD))
            held |= IRInput::kModifierShift;
        if (checkButton(IRInput::kKeyButtonLeftControl, ButtonStatuses::HELD))
            held |= IRInput::kModifierControl;
        if (checkButton(IRInput::kKeyButtonLeftAlt, ButtonStatuses::HELD))
            held |= IRInput::kModifierAlt;
        return held;
    }

  private:
    std::array<ButtonStatuses, static_cast<std::size_t>(IRInput::kNumKeyMouseButtons)> m_states;
};

SnapshotInput *g_input = nullptr;

bool probeButton(KeyMouseButtons button, ButtonStatuses status) {
    return g_input->checkButton(button, status);
}

bool probeModifiers(KeyModifierMask required, KeyModifierMask blocked) {
    return IRInput::modifiersMatch(required, blocked, g_input->heldModifiers());
}

class ActionRegistryTest : public testing::Test {
  protected:
    ActionRegistryTest() {
        g_input = &m_input;
    }

    ~ActionRegistryTest() override {
        g_input = nullptr;
    }

    void tick() {
        m_commands.executeUserKeyboardCommands(
            IRCommand::KeyMouseInputProbe{&probeButton, &probeModifiers}
        );
        m_input.advance();
    }

    void tap(KeyMouseButtons key) {
        m_input.press(key);
        tick();
        m_input.release(key);
        tick();
    }

    void tapChord(KeyMouseButtons modifier, KeyMouseButtons key) {
        m_input.press(modifier);
        tick();
        tap(key);
        m_input.release(modifier);
        tick();
    }

    void registerCounter(
        const std::string &id, std::vector<KeyChord> defaults, std::string context = "global"
    ) {
        ActionSpec spec;
        spec.id_ = id;
        spec.label_ = id;
        spec.section_ = "TEST";
        spec.context_ = std::move(context);
        spec.defaultChords_ = std::move(defaults);
        spec.invoke_ = [this, id]() { ++m_fired[id]; };
        ASSERT_TRUE(m_commands.actions().registerAction(std::move(spec)));
    }

    int fired(const std::string &id) {
        return m_fired[id];
    }

    bool registrationNamed(const std::string &name, KeyChord chord) const {
        for (const IRCommand::CommandRegistration &row : m_commands.getCommandRegistrations()) {
            if (row.name == name && row.button == static_cast<int>(chord.key_) &&
                row.requiredModifiers == chord.modifiers_) {
                return true;
            }
        }
        return false;
    }

    IRCommand::ActionRegistry &actions() {
        return m_commands.actions();
    }

    SnapshotInput m_input;
    IRCommand::CommandManager m_commands;
    std::map<std::string, int> m_fired;
};

constexpr KeyChord kK{IRInput::kKeyButtonK, IRInput::kModifierNone};
constexpr KeyChord kJ{IRInput::kKeyButtonJ, IRInput::kModifierNone};
constexpr KeyChord kL{IRInput::kKeyButtonL, IRInput::kModifierNone};
constexpr KeyChord kCtrlK{IRInput::kKeyButtonK, IRInput::kModifierControl};

TEST_F(ActionRegistryTest, IdAndKeyRunTheSameOperation) {
    registerCounter("test.stamp", {kK});
    EXPECT_TRUE(IRCommand::invokeAction("test.stamp"));
    EXPECT_EQ(fired("test.stamp"), 1);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 2);
    EXPECT_FALSE(IRCommand::invokeAction("test.missing"));
}

TEST_F(ActionRegistryTest, UnboundActionStillRunsById) {
    registerCounter("test.unbound", {});
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.unbound"), 0);
    EXPECT_TRUE(IRCommand::invokeAction("test.unbound"));
    EXPECT_EQ(fired("test.unbound"), 1);
    EXPECT_TRUE(actions().findAction("test.unbound")->chords_.empty());
}

TEST_F(ActionRegistryTest, ReassignMovesDispatchAndIntrospection) {
    registerCounter("test.stamp", {kK});
    EXPECT_TRUE(registrationNamed("test.stamp", kK));

    ASSERT_TRUE(actions().replaceChord("test.stamp", kK, kJ).ok());
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 0) << "the old chord must stop firing";
    tap(IRInput::kKeyButtonJ);
    EXPECT_EQ(fired("test.stamp"), 1) << "the new chord fires exactly once";
    EXPECT_EQ(actions().findAction("test.stamp")->chords_, std::vector<KeyChord>{kJ});
    EXPECT_FALSE(registrationNamed("test.stamp", kK));
    EXPECT_TRUE(registrationNamed("test.stamp", kJ));
    EXPECT_TRUE(actions().findAction("test.stamp")->customized_);
}

TEST_F(ActionRegistryTest, ReplaceOntoAnotherBoundChordKeepsOneCopy) {
    registerCounter("test.stamp", {kK, kJ, kL});
    ASSERT_TRUE(actions().replaceChord("test.stamp", kK, kL).ok());
    EXPECT_EQ(actions().findAction("test.stamp")->chords_, (std::vector<KeyChord>{kJ, kL}))
        << "the replaced chord goes and the already-bound target is not duplicated";
    tap(IRInput::kKeyButtonL);
    EXPECT_EQ(fired("test.stamp"), 1) << "one physical press fires the operation once";
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 1);
    EXPECT_EQ(actions().overrides().at("test.stamp"), (std::vector<KeyChord>{kJ, kL}));
}

TEST_F(ActionRegistryTest, ClearSecondChordAndRestoreDefaults) {
    registerCounter("test.stamp", {kK});
    const std::uint32_t before = m_commands.getRegistrationGeneration();

    ASSERT_TRUE(actions().addChord("test.stamp", kCtrlK).ok());
    EXPECT_GT(m_commands.getRegistrationGeneration(), before);
    tap(IRInput::kKeyButtonK);
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 2) << "both chords fire, each once";

    ASSERT_TRUE(actions().clearChords("test.stamp").ok());
    tap(IRInput::kKeyButtonK);
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 2);
    EXPECT_FALSE(registrationNamed("test.stamp", kK));

    ASSERT_TRUE(actions().restoreDefaults("test.stamp").ok());
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 3);
    EXPECT_FALSE(actions().findAction("test.stamp")->customized_);
    EXPECT_TRUE(actions().overrides().empty());
    EXPECT_EQ(actions().firstChord("test.stamp"), kK);
    ASSERT_TRUE(actions().clearChords("test.stamp").ok());
    EXPECT_FALSE(actions().firstChord("test.stamp"));
    EXPECT_FALSE(actions().firstChord("test.missing"));
}

TEST_F(ActionRegistryTest, ChordMaskIsExact) {
    registerCounter("test.plain", {kK});
    registerCounter("test.ctrl", {kCtrlK});
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.ctrl"), 1);
    EXPECT_EQ(fired("test.plain"), 0) << "a bare chord stays silent under a modifier";
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.ctrl"), 1);
    EXPECT_EQ(fired("test.plain"), 1);
}

TEST_F(ActionRegistryTest, ConflictRejectsThenReplaces) {
    registerCounter("test.a", {kK});
    registerCounter("test.b", {kJ});

    const IRCommand::BindResult rejected = actions().addChord("test.b", kK);
    EXPECT_EQ(rejected.status_, BindStatus::CONFLICT);
    ASSERT_EQ(rejected.conflicts_.size(), 1u);
    EXPECT_EQ(rejected.conflicts_[0].actionId_, "test.a");
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.a"), 1);
    EXPECT_EQ(fired("test.b"), 0);

    ASSERT_TRUE(actions().addChord("test.b", kK, ConflictPolicy::REPLACE).ok());
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.a"), 1) << "the displaced action no longer fires";
    EXPECT_EQ(fired("test.b"), 1) << "the new holder fires once";
    EXPECT_TRUE(actions().findAction("test.a")->chords_.empty());
}

TEST_F(ActionRegistryTest, PlainCommandRowCannotBeDisplaced) {
    m_commands.createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonK, []() {});
    registerCounter("test.a", {});
    const IRCommand::BindResult result = actions().addChord("test.a", kK, ConflictPolicy::REPLACE);
    EXPECT_EQ(result.status_, BindStatus::CONFLICT);
    ASSERT_EQ(result.conflicts_.size(), 1u);
    EXPECT_TRUE(result.conflicts_[0].actionId_.empty());
    EXPECT_TRUE(actions().addChord("test.a", kCtrlK).ok())
        << "a chord shadows the bare row by specificity, so it is not a conflict";
}

TEST_F(ActionRegistryTest, ContextsScopeConflictsAndDispatch) {
    registerCounter("test.paint", {kK}, "editor.paint");
    registerCounter("test.rig", {}, "editor.rig");
    EXPECT_TRUE(actions().addChord("test.rig", kK).ok())
        << "the same chord may live in two disjoint contexts";
    registerCounter("test.global", {});
    EXPECT_EQ(actions().addChord("test.global", kK).status_, BindStatus::CONFLICT)
        << "a global action overlaps every context";

    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.paint") + fired("test.rig"), 0) << "inactive contexts stay silent";
    actions().setContextActive("editor.paint", true);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.paint"), 1);
    EXPECT_EQ(fired("test.rig"), 0);
    actions().setContextActive("editor.paint", false);
    actions().setContextActive("editor.rig", true);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.paint"), 1);
    EXPECT_EQ(fired("test.rig"), 1);
}

TEST_F(ActionRegistryTest, AvailabilityGatesKeyAndId) {
    bool enabled = false;
    ActionSpec spec;
    spec.id_ = "test.gated";
    spec.defaultChords_ = {kK};
    spec.invoke_ = [this]() { ++m_fired["test.gated"]; };
    spec.available_ = [&enabled]() { return enabled; };
    ASSERT_TRUE(actions().registerAction(std::move(spec)));
    tap(IRInput::kKeyButtonK);
    EXPECT_FALSE(IRCommand::invokeAction("test.gated"));
    EXPECT_EQ(fired("test.gated"), 0);
    EXPECT_FALSE(actions().findAction("test.gated")->available_);
    enabled = true;
    tap(IRInput::kKeyButtonK);
    EXPECT_TRUE(IRCommand::invokeAction("test.gated"));
    EXPECT_EQ(fired("test.gated"), 2);
}

TEST_F(ActionRegistryTest, KeyboardCaptureSuppressesActions) {
    registerCounter("test.stamp", {kK});
    m_commands.setKeyboardCaptured(true);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 0) << "a focused text field captures the key";
    m_commands.setKeyboardCaptured(false);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(fired("test.stamp"), 1);
}

class HeldActionTest : public ActionRegistryTest {
  protected:
    void registerHold(std::vector<KeyChord> defaults) {
        ActionSpec spec;
        spec.id_ = "test.hold";
        spec.defaultChords_ = std::move(defaults);
        spec.invoke_ = [this]() { ++m_starts; };
        spec.release_ = [this]() { ++m_ends; };
        ASSERT_TRUE(actions().registerAction(std::move(spec)));
    }

    int m_starts = 0;
    int m_ends = 0;
};

TEST_F(HeldActionTest, PressStartsAndReleaseEndsOnce) {
    registerHold({kK});
    m_input.press(IRInput::kKeyButtonK);
    tick();
    tick();
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 0);
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1);
    EXPECT_TRUE(actions().findAction("test.hold")->held_);
}

TEST_F(HeldActionTest, RebindDuringHoldBalancesTheRelease) {
    registerHold({kK});
    m_input.press(IRInput::kKeyButtonK);
    tick();
    ASSERT_EQ(m_starts, 1);
    ASSERT_TRUE(actions().replaceChord("test.hold", kK, kJ).ok());
    EXPECT_EQ(m_ends, 1) << "the binding change ends the live hold itself";
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1) << "the old key's release must not end it a second time";
    tap(IRInput::kKeyButtonJ);
    EXPECT_EQ(m_starts, 2);
    EXPECT_EQ(m_ends, 2);
}

TEST_F(HeldActionTest, SecondChordPressedMidHoldJoinsIt) {
    registerHold({kK, kJ});
    m_input.press(IRInput::kKeyButtonK);
    tick();
    m_input.press(IRInput::kKeyButtonJ);
    tick();
    EXPECT_EQ(m_starts, 1) << "a second chord joins the live hold";
    m_input.release(IRInput::kKeyButtonJ);
    tick();
    EXPECT_EQ(m_ends, 0) << "the chord that started the hold is still down";
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1);

    m_input.press(IRInput::kKeyButtonK);
    tick();
    m_input.press(IRInput::kKeyButtonJ);
    tick();
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1) << "the joined chord keeps the hold alive";
    m_input.release(IRInput::kKeyButtonJ);
    tick();
    EXPECT_EQ(m_starts, 2);
    EXPECT_EQ(m_ends, 2) << "the hold ends once, on the last admitted release";
}

TEST_F(HeldActionTest, ReleaseOfAChordTheHoldNeverAdmittedIsIgnored) {
    registerHold({kK, kJ});
    m_input.press(IRInput::kKeyButtonJ);
    tick();
    ASSERT_EQ(m_starts, 1);
    m_commands.setKeyboardCaptured(true);
    m_input.press(IRInput::kKeyButtonK);
    tick();
    m_commands.setKeyboardCaptured(false);
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 0) << "K's press was captured, so its release cannot end J's hold";
    m_input.release(IRInput::kKeyButtonJ);
    tick();
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
}

TEST_F(HeldActionTest, ModifierLiftedFirstStillEndsTheHold) {
    registerHold({kCtrlK});
    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonK);
    tick();
    ASSERT_EQ(m_starts, 1);
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1);
}

TEST_F(HeldActionTest, CaptureStillReleasesAHoldBegunBefore) {
    registerHold({kK});
    m_input.press(IRInput::kKeyButtonK);
    tick();
    m_commands.setKeyboardCaptured(true);
    m_input.release(IRInput::kKeyButtonK);
    tick();
    EXPECT_EQ(m_ends, 1);
}

TEST_F(HeldActionTest, InvokeByIdRunsBothHalves) {
    registerHold({});
    EXPECT_TRUE(IRCommand::invokeAction("test.hold"));
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
}

TEST_F(ActionRegistryTest, BareActionDoesNotUnpairCameraStyleRows) {
    int velocity = 0;
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonW,
        [&velocity]() { velocity -= 20; }
    );
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::RELEASED,
        IRInput::kKeyButtonW,
        [&velocity]() { velocity += 20; }
    );
    registerCounter("test.save", {KeyChord{IRInput::kKeyButtonW, IRInput::kModifierControl}});
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonW);
    EXPECT_EQ(fired("test.save"), 1);
    EXPECT_EQ(velocity, 0) << "a chord shadows both halves of the pan pair or neither";
    tap(IRInput::kKeyButtonW);
    EXPECT_EQ(velocity, 0);
}

TEST_F(ActionRegistryTest, ReRegistrationKeepsUserBindings) {
    registerCounter("test.stamp", {kK});
    ASSERT_TRUE(actions().replaceChord("test.stamp", kK, kJ).ok());
    int second = 0;
    ActionSpec spec;
    spec.id_ = "test.stamp";
    spec.defaultChords_ = {kK};
    spec.invoke_ = [&second]() { ++second; };
    ASSERT_TRUE(actions().registerAction(std::move(spec)));
    tap(IRInput::kKeyButtonJ);
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(second, 1) << "the recreated owner's callable fires on the user's chord";
    EXPECT_EQ(fired("test.stamp"), 0);
}

TEST_F(ActionRegistryTest, SearchMatchesNameAndShortcut) {
    ActionSpec spec;
    spec.id_ = "test.paint";
    spec.label_ = "PAINT TOOL";
    spec.section_ = "TOOLS";
    spec.defaultChords_ = {kCtrlK};
    spec.invoke_ = []() {};
    ASSERT_TRUE(actions().registerAction(spec));
    spec.id_ = "test.erase";
    spec.label_ = "ERASE TOOL";
    spec.defaultChords_ = {kJ};
    ASSERT_TRUE(actions().registerAction(spec));

    EXPECT_EQ(actions().searchActions("paint").size(), 1u);
    EXPECT_EQ(actions().searchActions("TOOL").size(), 2u);
    const auto byChord = actions().searchActions("ctrl+k");
    ASSERT_EQ(byChord.size(), 1u);
    EXPECT_EQ(byChord[0].id_, "test.paint");
    EXPECT_EQ(actions().searchActions("").size(), 2u);
}

TEST_F(ActionRegistryTest, AdoptedNamedCommandsBecomeAssignableActions) {
    int undo = 0;
    int redo = 0;
    const IRCommand::CommandId undoId = m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&undo]() { ++undo; },
        IRInput::kModifierControl,
        IRInput::kModifierShift,
        "UNDO",
        "UNDO THE LAST EDIT"
    );
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonY,
        [&redo]() { ++redo; },
        IRInput::kModifierControl,
        IRInput::kModifierNone,
        "REDO",
        "REDO"
    );
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&redo]() { redo += 10; },
        IRInput::kModifierControl | IRInput::kModifierShift,
        IRInput::kModifierNone,
        "REDO",
        "REDO"
    );
    EXPECT_EQ(actions().adoptNamedCommands("editor"), 3u);
    ASSERT_TRUE(actions().isRegistered("editor.undo"));
    const auto redoInfo = actions().findAction("editor.redo");
    ASSERT_TRUE(redoInfo);
    EXPECT_EQ(redoInfo->chords_.size(), 2u) << "same-named bindings merge into one action";

    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonZ);
    EXPECT_EQ(undo, 1);
    m_input.press(IRInput::kKeyButtonLeftShift);
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonZ);
    m_input.release(IRInput::kKeyButtonLeftShift);
    tick();
    EXPECT_EQ(undo, 1) << "the adopted chord keeps its original blocked mask";
    EXPECT_EQ(redo, 10) << "each adopted chord keeps firing its own body";

    IRCommand::fire(undoId);
    EXPECT_EQ(undo, 2) << "a runtime handle held by the caller still fires";

    ASSERT_TRUE(
        actions()
            .replaceChord(
                "editor.undo",
                KeyChord{IRInput::kKeyButtonZ, IRInput::kModifierControl},
                kCtrlK
            )
            .ok()
    );
    tapChord(IRInput::kKeyButtonLeftControl, IRInput::kKeyButtonK);
    EXPECT_EQ(undo, 3);
    EXPECT_EQ(m_commands.getCommandRegistrations().size(), 3u)
        << "adopted rows publish once, as actions";
}

TEST_F(ActionRegistryTest, AdoptionLeavesStartEndPairsAlone) {
    int velocity = 0;
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonA,
        [&velocity]() { velocity -= 20; },
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "CAMERA LEFT",
        ""
    );
    m_commands.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::RELEASED,
        IRInput::kKeyButtonA,
        [&velocity]() { velocity += 20; }
    );
    EXPECT_EQ(actions().adoptNamedCommands("camera"), 0u);
    EXPECT_FALSE(actions().isRegistered("camera.camera_left"));
    tap(IRInput::kKeyButtonA);
    EXPECT_EQ(velocity, 0);
}

TEST_F(ActionRegistryTest, RowMutatorsRejectAnIdThatNamesNoRow) {
    const IRCommand::CommandId missing = 7;
    EXPECT_THROW(
        m_commands.rebindUserCommand(missing, IRInput::kKeyButtonK, 0, 0),
        std::runtime_error
    );
    EXPECT_THROW(m_commands.setUserCommandActive(missing, false), std::runtime_error);
}

TEST(ActionChordTokenTest, RoundTripsAndRejectsUnknown) {
    const KeyChord chord{IRInput::kKeyButtonP, IRInput::kModifierControl | IRInput::kModifierShift};
    EXPECT_EQ(IRCommand::chordToken(chord), "shift+ctrl+P");
    EXPECT_EQ(IRCommand::parseChordToken("shift+ctrl+P"), chord);
    EXPECT_EQ(IRCommand::parseChordToken("CTRL+SHIFT+p"), chord);
    const KeyChord plus{IRInput::kKeyButtonKPAdd, IRInput::kModifierAlt};
    EXPECT_EQ(IRCommand::parseChordToken(IRCommand::chordToken(plus)), plus);
    EXPECT_FALSE(IRCommand::parseChordToken("hyper+K"));
    EXPECT_FALSE(IRCommand::parseChordToken("ctrl+"));
    EXPECT_FALSE(IRCommand::isAssignableChord({IRInput::kKeyButtonLeftShift, 0}));
    EXPECT_FALSE(IRCommand::isAssignableChord({IRInput::kMouseButtonLeft, 0}));
}

} // namespace
