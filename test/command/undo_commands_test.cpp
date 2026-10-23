#include <gtest/gtest.h>

#include <irreden/command/command_manager.hpp>
#include <irreden/common/undo.hpp>
#include <irreden/ir_entity.hpp>

#include <array>
#include <string>
#include <vector>

namespace {

struct InputState {
    std::array<bool, IRInput::kNumKeyMouseButtons> down{};
    std::array<bool, IRInput::kNumKeyMouseButtons> pressed{};
    std::array<bool, IRInput::kNumKeyMouseButtons> released{};
};

InputState *g_input = nullptr;

bool checkButton(IRInput::KeyMouseButtons button, IRInput::ButtonStatuses status) {
    const std::size_t index = static_cast<std::size_t>(button);
    if (status == IRInput::PRESSED) {
        return g_input->pressed[index];
    }
    if (status == IRInput::RELEASED) {
        return g_input->released[index];
    }
    return g_input->down[index];
}

bool checkModifiers(IRInput::KeyModifierMask required, IRInput::KeyModifierMask blocked) {
    IRInput::KeyModifierMask active = IRInput::kModifierNone;
    if (g_input->down[IRInput::kKeyButtonLeftControl]) {
        active = active | IRInput::kModifierControl;
    }
    if (g_input->down[IRInput::kKeyButtonLeftShift]) {
        active = active | IRInput::kModifierShift;
    }
    return (active & required) == required && (active & blocked) == 0;
}

class UndoCommandsTest : public testing::Test {
  protected:
    UndoCommandsTest() {
        g_input = &m_input;
        IRPrefab::Undo::registerUndoCommands();
        m_value = 1;
        IRPrefab::Undo::push({"edit", [this]() { m_value = 0; }, [this]() { m_value = 1; }, {}});
    }

    ~UndoCommandsTest() override {
        g_input = nullptr;
    }

    void press(IRInput::KeyMouseButtons button) {
        m_input.down[button] = true;
        m_input.pressed[button] = true;
        tick();
    }

    void release(IRInput::KeyMouseButtons button) {
        m_input.down[button] = false;
        m_input.released[button] = true;
        tick();
    }

    void tick() {
        m_commandManager.executeUserKeyboardCommands({&checkButton, &checkModifiers});
        m_input.pressed.fill(false);
        m_input.released.fill(false);
    }

    void chord(IRInput::KeyMouseButtons key, bool shift = false) {
        press(IRInput::kKeyButtonLeftControl);
        if (shift) {
            press(IRInput::kKeyButtonLeftShift);
        }
        press(key);
        release(key);
        if (shift) {
            release(IRInput::kKeyButtonLeftShift);
        }
        release(IRInput::kKeyButtonLeftControl);
    }

    IREntity::EntityManager m_entityManager;
    IRCommand::CommandManager m_commandManager;
    InputState m_input;
    int m_value = 0;
};

TEST_F(UndoCommandsTest, RegistersNamedOverlayRows) {
    std::vector<std::string> rows;
    for (const auto &registration : m_commandManager.getCommandRegistrations()) {
        rows.push_back(
            IRCommand::modifierString(registration.requiredModifiers) + registration.name
        );
    }
    EXPECT_EQ(rows, (std::vector<std::string>{"CTRL+UNDO", "CTRL+REDO", "SHIFT+CTRL+REDO"}));
}

TEST_F(UndoCommandsTest, CtrlZCtrlYAndCtrlShiftZMoveHistory) {
    chord(IRInput::kKeyButtonZ);
    EXPECT_EQ(m_value, 0);
    chord(IRInput::kKeyButtonY);
    EXPECT_EQ(m_value, 1);
    chord(IRInput::kKeyButtonZ);
    EXPECT_EQ(m_value, 0);
    chord(IRInput::kKeyButtonZ, true);
    EXPECT_EQ(m_value, 1);
}

TEST_F(UndoCommandsTest, KeyboardCaptureSuppressesEveryChord) {
    m_commandManager.setKeyboardCaptured(true);
    chord(IRInput::kKeyButtonZ);
    EXPECT_EQ(m_value, 1);
    ASSERT_TRUE(IRPrefab::Undo::undo());
    ASSERT_EQ(m_value, 0);
    chord(IRInput::kKeyButtonY);
    chord(IRInput::kKeyButtonZ, true);
    EXPECT_EQ(m_value, 0);
}

} // namespace
