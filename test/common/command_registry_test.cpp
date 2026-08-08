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

// `isButtonBound` (#2570) reads `m_userCommands`, NOT the registration map —
// so the two row classes the registry filters out (unnamed, and non-PRESSED)
// must both be visible to it. Those arms are what discriminate a correct
// implementation: an `isButtonBound` written over `getCommandRegistrations()`
// is structurally unable to pass either, since neither row is ever recorded
// there. The registry emptiness assertions below are the proof that the rows
// really are registry-invisible, so a passing arm can't be read as the
// registry happening to carry them.
TEST_F(CommandRegistryTest, IsButtonBoundSeesUnnamedAndReleasedBindings) {
    // Unnamed PRESSED: the default shape of every ad-hoc lambda binding.
    m_commandManager.createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA, noopBody);
    // Named RELEASED: mirrors the camera suite's MOVE_CAMERA_*_END rows.
    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::RELEASED,
        kTestButtonB,
        noopBody,
        IRInput::kModifierNone,
        IRInput::kModifierNone,
        "RELEASE HALF"
    );

    ASSERT_TRUE(m_commandManager.getCommandRegistrations().empty())
        << "both bindings must be registry-invisible for this test to discriminate";

    EXPECT_TRUE(m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA));
    EXPECT_TRUE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::RELEASED, kTestButtonB)
    );

    // The same keys at the status they were NOT bound at: the scan matches on
    // all three of (inputType, status, button), so neither cross-matches.
    EXPECT_FALSE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::RELEASED, kTestButtonA)
    );
    EXPECT_FALSE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonB)
    );

    // A key nothing bound, and a bound key on a different device class.
    EXPECT_FALSE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonF12)
    );
    EXPECT_FALSE(m_commandManager.isButtonBound(IRInput::GAMEPAD, IRInput::PRESSED, kTestButtonA));
}

// Modifier-blindness is the documented contract, not an oversight: a caller
// guarding an ad-hoc bind wants "is this key taken", whatever mask sits behind
// it. Locked so a later modifier-aware refinement has to be a deliberate
// overload rather than a silent narrowing of this query.
TEST_F(CommandRegistryTest, IsButtonBoundIgnoresModifierMasks) {
    m_commandManager.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        kTestButtonA,
        noopBody,
        IRInput::kModifierControl,
        IRInput::kModifierShift
    );

    EXPECT_TRUE(m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA));
}

// The `ir_command.hpp` free function is its own public surface — the module
// entry point a C++ creation calls, resolving the manager through the
// `g_commandManager` global rather than holding the instance. The fixture's
// stack-local manager stamps that global in its ctor, so this covers the
// forwarding path the Lua binding also rides.
TEST_F(CommandRegistryTest, ModuleApiIsButtonBoundForwardsToTheActiveManager) {
    EXPECT_FALSE(IRCommand::isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA));

    m_commandManager.createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA, noopBody);

    EXPECT_TRUE(IRCommand::isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA));
    EXPECT_FALSE(IRCommand::isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonB));
}

// The type dimension, in the direction the sibling arms above don't cover: a
// row bound under a non-`KEY_MOUSE` input type is invisible to a `KEY_MOUSE`
// query. The `EXPECT_TRUE` is the was-seen arm — without it the `EXPECT_FALSE`
// would also pass on a manager that never stored the row at all.
//
// Worth locking because it is exactly where the query and the dispatcher part
// company: `executeUserKeyboardCommandsAll` ignores `getType()` and would fire
// this row on a real F5 press. Nothing in the tree binds a non-`KEY_MOUSE`
// button, so that gap is latent — but a future "simplification" that drops the
// type check here would silently change what `isButtonBound` promises, and the
// fix for the gap belongs on the dispatcher's side. See
// `engine/command/CLAUDE.md` §"Querying what is bound".
TEST_F(CommandRegistryTest, IsButtonBoundIsTypeExact) {
    m_commandManager.createCommand(IRInput::GAMEPAD, IRInput::PRESSED, kTestButtonA, noopBody);

    EXPECT_TRUE(m_commandManager.isButtonBound(IRInput::GAMEPAD, IRInput::PRESSED, kTestButtonA))
        << "the row must exist for the negative arm below to mean anything";
    EXPECT_FALSE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA)
    );
}

// An empty manager reports nothing bound — the "in a creation that never
// registers the camera suite it returns false" half of the acceptance
// criteria, at the C++ level.
TEST_F(CommandRegistryTest, IsButtonBoundIsFalseOnAFreshManager) {
    EXPECT_FALSE(
        m_commandManager.isButtonBound(IRInput::KEY_MOUSE, IRInput::PRESSED, kTestButtonA)
    );
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
