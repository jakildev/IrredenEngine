#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>

#include <string>

// Covers the introspectable command registry (#2550) — the read-only
// `(binding, name, description)` surface the F1 help overlay renders and
// #2551's settings menu consumes.
//
// Three properties, each a defect this change fixes or a filter it must keep:
//
//  1. A named + described PRESSED binding lands in the registry with BOTH
//     strings, and bumps the registration generation.
//  2. The generation bump is the cache-invalidation signal for the overlay: a
//     late registration MUST bump, or a command registered after the first
//     visible frame never appears; a filtered one must NOT, or "zero-cost
//     while hidden" degrades into a per-frame rebuild.
//  3. Every `CommandNames` value has a `kCommandInfo` row, so no value can
//     render as "UNKNOWN". The static_asserts in `ir_command.hpp` are the real
//     guard; this documents the contract and catches a regression that
//     replaced them with a runtime lookup.
//
// Headless: `CommandManager`'s ctor stamps `g_commandManager`, so a
// stack-local instance is the whole fixture — no `World`, no render context.

namespace {

// Distinct unbound keys per case so registrations can't collide across tests
// sharing a process.
constexpr int kTestButtonA = IRInput::kKeyButtonF5;
constexpr int kTestButtonB = IRInput::kKeyButtonF6;

class CommandRegistryTest : public ::testing::Test {
  protected:
    IRCommand::CommandManager m_commandManager;

    static void noopBody() {}
};

TEST_F(CommandRegistryTest, NamedPressedRegistrationCarriesNameAndDescription) {
    EXPECT_TRUE(m_commandManager.getCommandRegistrations().empty());

    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        kTestButtonA,
        noopBody,
        IRInput::kModifierShift,
        IRInput::kModifierNone,
        "TEST COMMAND",
        "DOES THE TEST THING"
    );

    const auto &registrations = m_commandManager.getCommandRegistrations();
    ASSERT_EQ(registrations.size(), 1u);
    EXPECT_EQ(registrations[0].name, "TEST COMMAND");
    EXPECT_EQ(registrations[0].description, "DOES THE TEST THING");
    EXPECT_EQ(registrations[0].button, kTestButtonA);
    EXPECT_EQ(registrations[0].triggerStatus, IRInput::PRESSED);
    EXPECT_EQ(registrations[0].requiredModifiers, IRInput::kModifierShift);
}

// A described-less registration is still listed — the overlay renders the
// binding + name and simply omits the description clause.
TEST_F(CommandRegistryTest, NamedRegistrationWithoutDescriptionIsListed) {
    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        kTestButtonA,
        noopBody,
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "NAME ONLY"
    );

    const auto &registrations = m_commandManager.getCommandRegistrations();
    ASSERT_EQ(registrations.size(), 1u);
    EXPECT_EQ(registrations[0].name, "NAME ONLY");
    EXPECT_TRUE(registrations[0].description.empty());
}

// The regression lock on the cache-forever bug: a command registered after the
// overlay has already built its text must change the generation, or it never
// appears.
TEST_F(CommandRegistryTest, LateNamedRegistrationBumpsGeneration) {
    const std::uint32_t initial = m_commandManager.getRegistrationGeneration();

    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        kTestButtonA,
        noopBody,
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "FIRST"
    );
    const std::uint32_t afterFirst = m_commandManager.getRegistrationGeneration();
    EXPECT_NE(afterFirst, initial);

    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        kTestButtonB,
        noopBody,
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "SECOND"
    );
    EXPECT_NE(m_commandManager.getRegistrationGeneration(), afterFirst);
    EXPECT_EQ(m_commandManager.getCommandRegistrations().size(), 2u);
}

// The two filters that keep the overlay readable: an unnamed ad-hoc lambda
// binding stays out, and so does a non-PRESSED half of a start/end key pair
// (the WASD `*_END` RELEASED bindings). Neither may bump the generation —
// a bump with no registry change would rebuild the overlay text every frame.
TEST_F(CommandRegistryTest, UnnamedAndNonPressedRegistrationsAreFilteredWithoutBump) {
    const std::uint32_t initial = m_commandManager.getRegistrationGeneration();

    // Unnamed: the default for every ad-hoc `createCommand(...)` call site.
    m_commandManager.createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA, noopBody);
    EXPECT_TRUE(m_commandManager.getCommandRegistrations().empty());
    EXPECT_EQ(m_commandManager.getRegistrationGeneration(), initial);

    // Named but RELEASED: intentionally hidden, mirroring MOVE_CAMERA_*_END.
    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::RELEASED,
        kTestButtonB,
        noopBody,
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "RELEASE HALF",
        "SHOULD NOT APPEAR"
    );
    EXPECT_TRUE(m_commandManager.getCommandRegistrations().empty());
    EXPECT_EQ(m_commandManager.getRegistrationGeneration(), initial);
}

// Completeness: no enum value may fall back to "UNKNOWN", and every value's
// row must sit at its own index (the table is indexed by enum value).
TEST(CommandCatalogTest, EveryCommandNameHasANonUnknownLabel) {
    for (int i = 0; i < IRCommand::kCommandNameCount; ++i) {
        const auto name = static_cast<IRCommand::CommandNames>(i);
        const std::string label = IRCommand::commandNameToString(name);
        EXPECT_FALSE(label.empty()) << "CommandNames value " << i << " has an empty label";
        EXPECT_NE(label, "UNKNOWN") << "CommandNames value " << i << " has no kCommandInfo row";
        EXPECT_EQ(static_cast<int>(IRCommand::kCommandInfo[i].name_), i)
            << "kCommandInfo row " << i << " describes a different enum value";
    }
}

// Out-of-range values (a bad integer cast in from Lua) degrade to the sentinel
// rather than indexing past the table.
TEST(CommandCatalogTest, OutOfRangeValueReturnsUnknownAndEmptyDescription) {
    const auto outOfRange = static_cast<IRCommand::CommandNames>(IRCommand::kCommandNameCount + 7);
    EXPECT_EQ(IRCommand::commandNameToString(outOfRange), "UNKNOWN");
    EXPECT_TRUE(IRCommand::commandDescription(outOfRange).empty());
}

// The camera bundle is what makes "adopt the overlay, get the base help for
// free" true — every `standardControlSystems()` demo shows these described
// with zero per-demo wiring. Guards against a future edit blanking them.
TEST(CommandCatalogTest, CameraSuiteCommandsCarryDescriptions) {
    for (const auto name :
         {IRCommand::CLOSE_WINDOW,
          IRCommand::ZOOM_IN,
          IRCommand::ZOOM_OUT,
          IRCommand::MOVE_CAMERA_UP_START,
          IRCommand::MOVE_CAMERA_DOWN_START,
          IRCommand::MOVE_CAMERA_LEFT_START,
          IRCommand::MOVE_CAMERA_RIGHT_START,
          IRCommand::TOGGLE_HELP_OVERLAY}) {
        EXPECT_FALSE(IRCommand::commandDescription(name).empty())
            << IRCommand::commandNameToString(name) << " must carry a help description";
    }
}

} // namespace
