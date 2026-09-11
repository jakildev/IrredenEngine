#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/command/command_manager.hpp>
#include <irreden/input/ir_input_types.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

// Headless regression lock for keyboard command dispatch.
//
// `CommandManager` reads live input through exactly two `IRInput` functions, so
// `executeUserKeyboardCommands(KeyMouseInputProbe)` — the entry point
// `executeUserKeyboardCommandsAll()` is a one-line wrapper over — runs the
// PRODUCTION selection algorithm against the deterministic button states below.
// No GLFW, no window, no `InputManager`, so nothing here can skip on a headless
// host the way the injection tier of `lua_input_bindings_test.cpp` does.
//
// The defect this pins: modifier specificity is decided per frame and keyed on
// the button, so a chord (Ctrl+S) shadowed the PRESSED half of the camera's
// S pan pair while the RELEASED half — which has no modifier-specific row to
// lose to on the release frame — still fired. The two halves accumulate into
// `C_Velocity2DIso` with `-=` / `+=`, so one unpaired `_END` left the camera
// panning forever at +20.

namespace {

using IRInput::ButtonStatuses;
using IRInput::KeyModifierMask;
using IRInput::KeyMouseButtons;

// Pan magnitude of `Command<MOVE_CAMERA_*_START/END>` — the real
// `kCameraMoveSpeed`, so a leftover half reads as the number the GUI probe
// reports.
constexpr int kPanSpeed = 20;

// Stand-in for the input layer, mirroring `InputManager`'s per-button state
// machine (`ir_input_types.hpp`) and the two predicates the dispatcher calls:
//
//   NOT_HELD → (press)                → PRESSED
//   PRESSED  → (held next frame)      → HELD
//   HELD     → (release)              → RELEASED
//   RELEASED → (idle next frame)      → NOT_HELD
//   PRESSED  → (release in same frame)→ PRESSED_AND_RELEASED
//
// `checkButton` reproduces `InputManager::checkButton` exactly — PRESSED and
// RELEASED both match PRESSED_AND_RELEASED, HELD means "down" — and
// `checkModifiers` reproduces `IRInput::checkKeyMouseModifiers`, deriving
// Shift/Ctrl/Alt from the same button states the real one reads. Stubbing the
// boundary is the whole point; the selection logic under test stays untouched.
//
// Both bodies are mirrors, and nothing compiles them against their originals:
// changing `InputManager::checkButton`, its three `checkButton*` helpers, or
// `IRInput::checkKeyMouseModifiers` means changing this class in the same pass,
// or the fixture keeps asserting against semantics the engine no longer has.
class FakeInput {
  public:
    FakeInput() {
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

    // The frame boundary: what `InputManager::advanceInputState` does to every
    // button once the frame's events have been consumed.
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

    bool checkModifiers(KeyModifierMask requiredModifiers, KeyModifierMask blockedModifiers) const {
        const bool shiftDown =
            isDown(IRInput::kKeyButtonLeftShift) || isDown(IRInput::kKeyButtonRightShift);
        const bool controlDown =
            isDown(IRInput::kKeyButtonLeftControl) || isDown(IRInput::kKeyButtonRightControl);
        const bool altDown =
            isDown(IRInput::kKeyButtonLeftAlt) || isDown(IRInput::kKeyButtonRightAlt);

        if ((requiredModifiers & IRInput::kModifierShift) != 0 && !shiftDown) {
            return false;
        }
        if ((requiredModifiers & IRInput::kModifierControl) != 0 && !controlDown) {
            return false;
        }
        if ((requiredModifiers & IRInput::kModifierAlt) != 0 && !altDown) {
            return false;
        }
        if ((blockedModifiers & IRInput::kModifierShift) != 0 && shiftDown) {
            return false;
        }
        if ((blockedModifiers & IRInput::kModifierControl) != 0 && controlDown) {
            return false;
        }
        if ((blockedModifiers & IRInput::kModifierAlt) != 0 && altDown) {
            return false;
        }
        return true;
    }

  private:
    bool isDown(KeyMouseButtons button) const {
        return checkButton(button, ButtonStatuses::HELD);
    }

    std::array<ButtonStatuses, static_cast<std::size_t>(IRInput::kNumKeyMouseButtons)> m_states;
};

// The probe is two plain function pointers (no captures), so the fixture hands
// itself to them through this TU-local pointer for the duration of each test.
FakeInput *g_fakeInput = nullptr;

bool probeCheckButton(KeyMouseButtons button, ButtonStatuses status) {
    return g_fakeInput->checkButton(button, status);
}

bool probeCheckModifiers(KeyModifierMask requiredModifiers, KeyModifierMask blockedModifiers) {
    return g_fakeInput->checkModifiers(requiredModifiers, blockedModifiers);
}

class KeyboardDispatchTest : public testing::Test {
  protected:
    KeyboardDispatchTest() {
        g_fakeInput = &m_input;
    }

    ~KeyboardDispatchTest() override {
        g_fakeInput = nullptr;
    }

    // One `World::update()` worth of dispatch: fire everything this frame's
    // input matches, then step the button state machine.
    void tick() {
        m_command_manager.executeUserKeyboardCommands(
            IRCommand::KeyMouseInputProbe{&probeCheckButton, &probeCheckModifiers}
        );
        m_input.advance();
    }

    // A camera pan axis: two DISTINCT bare-mask commands on one button, `-=`
    // on press and `+=` on release. This is `kCameraSuite`'s shape.
    // Returns the RELEASED half's id — the one `DirectFireBypassesAdmission`
    // fires imperatively.
    IRCommand::CommandId
    bindPanPair(KeyMouseButtons button, KeyModifierMask blockedModifiers = IRInput::kModifierNone) {
        bindPanStart(button, blockedModifiers);
        return bindPanEnd(button, blockedModifiers);
    }

    IRCommand::CommandId bindPanStart(
        KeyMouseButtons button, KeyModifierMask blockedModifiers = IRInput::kModifierNone
    ) {
        return m_command_manager.createCommand(
            IRInput::KEY_MOUSE,
            IRInput::PRESSED,
            button,
            [this]() {
                ++m_starts;
                m_velocity -= kPanSpeed;
                m_order.push_back("start");
            },
            IRInput::kModifierNone,
            blockedModifiers
        );
    }

    IRCommand::CommandId
    bindPanEnd(KeyMouseButtons button, KeyModifierMask blockedModifiers = IRInput::kModifierNone) {
        return m_command_manager.createCommand(
            IRInput::KEY_MOUSE,
            IRInput::RELEASED,
            button,
            [this]() {
                ++m_ends;
                m_velocity += kPanSpeed;
                m_order.push_back("end");
            },
            IRInput::kModifierNone,
            blockedModifiers
        );
    }

    // A modifier chord on the same key — the save binding whose real
    // `requiredModifiers` (the help overlay renders that field) arms
    // specificity in the first place.
    IRCommand::CommandId bindChord(
        KeyMouseButtons button,
        KeyModifierMask requiredModifiers,
        ButtonStatuses triggerStatus = IRInput::PRESSED
    ) {
        return m_command_manager.createCommand(
            IRInput::KEY_MOUSE,
            triggerStatus,
            button,
            [this]() {
                ++m_chords;
                m_order.push_back("chord");
            },
            requiredModifiers
        );
    }

    FakeInput m_input;
    IRCommand::CommandManager m_command_manager;

    int m_starts = 0;
    int m_ends = 0;
    int m_chords = 0;
    int m_velocity = 0;
    std::vector<std::string> m_order;
};

// ---- The defect: a chord on a pair's key must shadow both halves or neither --

// S released before Ctrl — the ordering `editor_probe_save` drives.
TEST_F(KeyboardDispatchTest, ChordHeldLeavesPairBalanced) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_chords, 1) << "the chord itself must still fire";
    EXPECT_EQ(m_starts, 0) << "specificity shadows the bare press";
    EXPECT_EQ(m_velocity, 0);

    tick(); // S held
    m_input.release(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();

    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_ends, 0) << "a shadowed start must not leave a live end behind";
    EXPECT_EQ(m_chords, 1);
    EXPECT_EQ(m_velocity, 0);
}

// The bare control arm: same bindings, no modifier. Both halves fire, and the
// intermediate velocity is non-zero while the key is down — proof the fixture
// can observe the effect it asserts away.
TEST_F(KeyboardDispatchTest, BareCycleFiresBothHalves) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_chords, 0);
    EXPECT_EQ(m_velocity, -kPanSpeed) << "the camera is panning while S is down";

    tick();
    EXPECT_EQ(m_velocity, -kPanSpeed) << "a held key re-fires neither half";

    m_input.release(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_velocity, 0);
}

TEST_F(KeyboardDispatchTest, CtrlReleasedBeforeSLeavesPairBalanced) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();
    EXPECT_EQ(m_velocity, 0) << "the rejected press must not start panning mid-hold";

    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_ends, 0) << "the release is governed by the press, not by live modifiers";
    EXPECT_EQ(m_chords, 1);
    EXPECT_EQ(m_velocity, 0);
}

TEST_F(KeyboardDispatchTest, SimultaneousReleaseLeavesPairBalanced) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    tick();
    m_input.release(IRInput::kKeyButtonS);
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();

    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_ends, 0);
    EXPECT_EQ(m_chords, 1);
    EXPECT_EQ(m_velocity, 0);
}

// The reverse asymmetry, which the editor's `blockedModifiers` workaround had
// instead: a bare press admits, then Ctrl arrives mid-hold. The pan must be
// observable while the key is down AND must stop on release.
TEST_F(KeyboardDispatchTest, CtrlPressedAfterBareStartStillCleansUp) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_velocity, -kPanSpeed);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    EXPECT_EQ(m_chords, 0) << "S is held, not pressed, so the chord does not fire";
    EXPECT_EQ(m_velocity, -kPanSpeed) << "still panning while held";

    m_input.release(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_ends, 1) << "a modifier pressed mid-hold must not swallow the cleanup";
    EXPECT_EQ(m_velocity, 0);
}

// Same shape, but the chord that appears mid-hold is a RELEASED row, so it
// matches on the very frame the pair needs its cleanup. Both fire: a
// release-only shortcut is still usable, it just cannot steal an admitted pair.
TEST_F(KeyboardDispatchTest, ModifierSpecificReleaseCannotStealAdmittedCleanup) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl, IRInput::RELEASED);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_chords, 1) << "the release-only chord still runs";
    EXPECT_EQ(m_velocity, 0);
}

TEST_F(KeyboardDispatchTest, ModifierSpecificHeldMidHoldDoesNotShadowAdmittedPair) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl, IRInput::HELD);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    EXPECT_EQ(m_chords, 1) << "Ctrl+S HELD matches while both are down";

    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_velocity, 0);
}

// A same-frame press+release (`PRESSED_AND_RELEASED`) is one admission driving
// both edges.
TEST_F(KeyboardDispatchTest, SameFramePressAndReleaseBareFiresBothHalves) {
    bindPanPair(IRInput::kKeyButtonS);

    m_input.press(IRInput::kKeyButtonS);
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 1);
    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_velocity, 0);
    EXPECT_EQ(m_order, (std::vector<std::string>{"start", "end"}));
}

TEST_F(KeyboardDispatchTest, SameFramePressAndReleaseWithCtrlHeldFiresNeitherHalf) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_ends, 0);
    EXPECT_EQ(m_chords, 1);
    EXPECT_EQ(m_velocity, 0);
}

// ---- Group identity -------------------------------------------------------

// Pairing is structural — button plus blocked mask — so the RELEASED row can be
// registered first without changing anything.
TEST_F(KeyboardDispatchTest, ReverseRegistrationOrderStillPairs) {
    bindPanEnd(IRInput::kKeyButtonS);
    bindPanStart(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_ends, 0);
    EXPECT_EQ(m_velocity, 0);
}

// Two rows on one key with DIFFERENT blocked masks are two groups, not a pair:
// a mask that rejects one of them has nothing to say about the other, so the
// caller's own asymmetry is preserved rather than silently latched over.
TEST_F(KeyboardDispatchTest, DifferentBlockedMasksAreNotAPair) {
    bindPanStart(IRInput::kKeyButtonS, IRInput::kModifierControl);
    bindPanEnd(IRInput::kKeyButtonS);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 0) << "the blocked mask rejects the press";

    m_input.release(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_ends, 1) << "the unmasked release row is its own group and still fires";
    EXPECT_EQ(m_velocity, kPanSpeed);
}

// The matching-mask form the editor used as a workaround: both halves carry the
// same blocked mask, so they ARE a pair and stay balanced whenever Ctrl moves.
TEST_F(KeyboardDispatchTest, MatchingBlockedMasksPairAndStayBalanced) {
    bindPanPair(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 1);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_ends, 1) << "the blocked-mask decision is frozen at the press too";
    EXPECT_EQ(m_velocity, 0);
}

TEST_F(KeyboardDispatchTest, ButtonsLatchIndependently) {
    bindPanPair(IRInput::kKeyButtonS);
    bindPanPair(IRInput::kKeyButtonW);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonW);
    tick();
    EXPECT_EQ(m_starts, 1) << "W is unaffected by anything bound to S";

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 1) << "S is shadowed";

    m_input.release(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonW);
    tick();

    EXPECT_EQ(m_ends, 1) << "only W's end fires";
    EXPECT_EQ(m_velocity, 0);
}

TEST_F(KeyboardDispatchTest, NextBareCycleAfterAShadowedOneFiresNormally) {
    bindPanPair(IRInput::kKeyButtonS);
    bindChord(IRInput::kKeyButtonS, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();
    ASSERT_EQ(m_starts, 0);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_starts, 1) << "the rejection does not persist past the release";
    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_velocity, 0);
}

// ---- Lifecycle ------------------------------------------------------------

// A pair formed while its key is already down has no admission behind that
// press, so its first eligible cycle is the NEXT one. Registration between
// frames must not synthesize a start or hand out an unearned cleanup.
TEST_F(KeyboardDispatchTest, RegistrationMidHoldDoesNotGrantCleanupForAnOldPress) {
    bindPanStart(IRInput::kKeyButtonS);

    m_input.press(IRInput::kKeyButtonS);
    tick();
    ASSERT_EQ(m_starts, 1) << "an unpaired PRESSED row fires on its own";

    bindPanEnd(IRInput::kKeyButtonS);
    m_input.release(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_ends, 0) << "the new pair has no admitted press to clean up";

    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();
    EXPECT_EQ(m_starts, 2);
    EXPECT_EQ(m_ends, 1) << "the next full cycle is governed normally";
}

// Admission is manager-owned state, so a fresh manager starts with none.
TEST_F(KeyboardDispatchTest, FreshManagerHasNoStandingAdmission) {
    {
        IRCommand::CommandManager firstManager;
        firstManager.createCommand(
            IRInput::KEY_MOUSE,
            IRInput::PRESSED,
            IRInput::kKeyButtonS,
            [this]() { ++m_starts; },
            IRInput::kModifierNone
        );
        firstManager.createCommand(
            IRInput::KEY_MOUSE,
            IRInput::RELEASED,
            IRInput::kKeyButtonS,
            [this]() { ++m_ends; },
            IRInput::kModifierNone
        );
        m_input.press(IRInput::kKeyButtonS);
        firstManager.executeUserKeyboardCommands(
            IRCommand::KeyMouseInputProbe{&probeCheckButton, &probeCheckModifiers}
        );
        m_input.advance();
        ASSERT_EQ(m_starts, 1);
    }

    bindPanPair(IRInput::kKeyButtonS);
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_ends, 0) << "the destroyed manager's admission cannot leak into this one";
}

// ---- Compatibility: everything that is not a bare pair is unchanged --------

TEST_F(KeyboardDispatchTest, UnpairedBindingsStillFireOnEveryStatus) {
    int pressedOnly = 0;
    int heldOnly = 0;
    int releasedOnly = 0;
    m_command_manager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&]() { ++pressedOnly; }
    );
    m_command_manager.createCommand(IRInput::KEY_MOUSE, IRInput::HELD, IRInput::kKeyButtonX, [&]() {
        ++heldOnly;
    });
    m_command_manager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::RELEASED,
        IRInput::kKeyButtonC,
        [&]() { ++releasedOnly; }
    );

    m_input.press(IRInput::kKeyButtonZ);
    m_input.press(IRInput::kKeyButtonX);
    m_input.press(IRInput::kKeyButtonC);
    tick();
    tick();
    m_input.release(IRInput::kKeyButtonX);
    m_input.release(IRInput::kKeyButtonC);
    tick();

    EXPECT_EQ(pressedOnly, 1);
    EXPECT_EQ(heldOnly, 2) << "HELD fires on the press frame and every frame down";
    EXPECT_EQ(releasedOnly, 1) << "a standalone release row needs no press behind it";
}

// Specificity itself is untouched for anything that is not a pair.
TEST_F(KeyboardDispatchTest, ChordStillShadowsAnUnpairedBareBinding) {
    int bare = 0;
    m_command_manager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&]() { ++bare; }
    );
    bindChord(IRInput::kKeyButtonZ, IRInput::kModifierControl);

    m_input.press(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonZ);
    tick();
    EXPECT_EQ(bare, 0);
    EXPECT_EQ(m_chords, 1);

    m_input.release(IRInput::kKeyButtonZ);
    m_input.release(IRInput::kKeyButtonLeftControl);
    tick();
    m_input.press(IRInput::kKeyButtonZ);
    tick();
    EXPECT_EQ(bare, 1) << "without the modifier the bare binding still fires";
}

TEST_F(KeyboardDispatchTest, SameSpecificityRowsKeepRegistrationOrderFanOut) {
    m_command_manager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonS,
        [this]() { m_order.push_back("first"); }
    );
    bindPanPair(IRInput::kKeyButtonS);
    m_command_manager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonS,
        [this]() { m_order.push_back("last"); }
    );

    m_input.press(IRInput::kKeyButtonS);
    tick();
    m_input.release(IRInput::kKeyButtonS);
    tick();

    EXPECT_EQ(m_order, (std::vector<std::string>{"first", "start", "last", "end"}))
        << "fan-out order is registration order; pairing does not reorder or dedupe";
}

// `fire` bypasses dispatch entirely, so it never acquires or consults a pair's
// admission — an imperative call is not a press.
TEST_F(KeyboardDispatchTest, DirectFireBypassesAdmission) {
    const IRCommand::CommandId endId = bindPanPair(IRInput::kKeyButtonS);

    m_command_manager.fireUserCommand(endId);

    EXPECT_EQ(m_ends, 1);
    EXPECT_EQ(m_starts, 0);
    EXPECT_EQ(m_velocity, kPanSpeed) << "fire is unconditional, exactly as before";
}

} // namespace
