#include <gtest/gtest.h>
#include <irreden/ir_args.hpp>

#include <string>
#include <vector>

namespace {

using namespace IRArgs;

// Turns a token list into the char** argv parse() expects. argv[0] is the
// (skipped) program name; a trailing nullptr terminates the array. Storage
// backs the pointers via std::string::data() (C++17 non-const overload) —
// parse() never writes through them.
struct Argv {
    std::vector<std::string> store_;
    std::vector<char *> ptrs_;

    explicit Argv(std::vector<std::string> tokens)
        : store_(std::move(tokens)) {
        for (auto &s : store_) {
            ptrs_.push_back(s.data());
        }
        ptrs_.push_back(nullptr);
    }

    int argc() const {
        return static_cast<int>(store_.size());
    }
    char **argv() {
        return ptrs_.data();
    }
};

// ─────────────────────────────────────────────
// numbers() / getFloats() — happy path
// ─────────────────────────────────────────────

TEST(IRArgsFloatListTest, SpaceSeparatedFormParsesInOrder) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 3);
    Argv a({"prog", "--sweep-yaw", "0", "360", "8"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--sweep-yaw");
    ASSERT_EQ(v.size(), 3u);
    EXPECT_FLOAT_EQ(v[0], 0.0f);
    EXPECT_FLOAT_EQ(v[1], 360.0f);
    EXPECT_FLOAT_EQ(v[2], 8.0f);
    EXPECT_TRUE(p.wasProvided("--sweep-yaw"));
}

TEST(IRArgsFloatListTest, InlineCommaFormParsesInOrder) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 3);
    Argv a({"prog", "--sweep-yaw=0,360,8"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--sweep-yaw");
    ASSERT_EQ(v.size(), 3u);
    EXPECT_FLOAT_EQ(v[0], 0.0f);
    EXPECT_FLOAT_EQ(v[1], 360.0f);
    EXPECT_FLOAT_EQ(v[2], 8.0f);
    EXPECT_TRUE(p.wasProvided("--sweep-yaw"));
}

TEST(IRArgsFloatListTest, DefaultsToAllZeroWhenAbsent) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 3);
    Argv a({"prog"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--sweep-yaw");
    ASSERT_EQ(v.size(), 3u);
    EXPECT_FLOAT_EQ(v[0], 0.0f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
    EXPECT_FLOAT_EQ(v[2], 0.0f);
    EXPECT_FALSE(p.wasProvided("--sweep-yaw"));
}

// ─────────────────────────────────────────────
// Malformed input — death tests (parse() exits on these branches)
// ─────────────────────────────────────────────

TEST(IRArgsFloatListDeathTest, SpaceSeparatedTooFewValuesExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 3);
    Argv a({"prog", "--sweep-yaw", "0", "360"});
    EXPECT_EXIT(
        p.parse(a.argc(), a.argv()),
        ::testing::ExitedWithCode(2),
        "expects 3 float values"
    );
}

TEST(IRArgsFloatListDeathTest, InlineWrongCountExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 3);
    Argv a({"prog", "--sweep-yaw=0,360"});
    EXPECT_EXIT(
        p.parse(a.argc(), a.argv()),
        ::testing::ExitedWithCode(2),
        "expects 3 comma-separated float values"
    );
}

TEST(IRArgsDeathTest, UnknownArgExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    Argv a({"prog", "--bogus"});
    EXPECT_EXIT(p.parse(a.argc(), a.argv()), ::testing::ExitedWithCode(2), "Unknown argument");
}

TEST(IRArgsDeathTest, MissingValueExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    p.integer("--grid-size", "Grid edge in cells", 64);
    Argv a({"prog", "--grid-size"});
    EXPECT_EXIT(
        p.parse(a.argc(), a.argv()),
        ::testing::ExitedWithCode(2),
        "expects an integer value"
    );
}

// ─────────────────────────────────────────────
// Accepted-as-malformed — trailing comma / garbage tokens (value assertions,
// NOT death tests: arity still matches, so parse() accepts and coerces to 0.0).
// ─────────────────────────────────────────────

TEST(IRArgsFloatListTest, InlineTrailingCommaParsesEmptyTailAsZero) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 2);
    Argv a({"prog", "--sweep-yaw=8,"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--sweep-yaw");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_FLOAT_EQ(v[0], 8.0f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
}

TEST(IRArgsFloatListTest, SpaceSeparatedNonNumericTokenParsesAsZero) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--sweep-yaw", "Yaw sweep", 2);
    Argv a({"prog", "--sweep-yaw", "8", "xyz"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--sweep-yaw");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_FLOAT_EQ(v[0], 8.0f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
}

// ─────────────────────────────────────────────
// Baseline regression — pre-existing types
// ─────────────────────────────────────────────

TEST(IRArgsFlagTest, DefaultsFalseAndSetsTrueWhenPresent) {
    Parser p(nullptr, Common::NONE);
    p.flag("--no-overlay", "Hide the debug overlay");
    Argv absent({"prog"});
    p.parse(absent.argc(), absent.argv());
    EXPECT_FALSE(p.getFlag("--no-overlay"));
    EXPECT_FALSE(p.wasProvided("--no-overlay"));

    Parser p2(nullptr, Common::NONE);
    p2.flag("--no-overlay", "Hide the debug overlay");
    Argv present({"prog", "--no-overlay"});
    p2.parse(present.argc(), present.argv());
    EXPECT_TRUE(p2.getFlag("--no-overlay"));
    EXPECT_TRUE(p2.wasProvided("--no-overlay"));
}

TEST(IRArgsIntTest, SpaceAndInlineFormsAndDefault) {
    Parser p(nullptr, Common::NONE);
    p.integer("--grid-size", "Grid edge in cells", 64);
    Argv absent({"prog"});
    p.parse(absent.argc(), absent.argv());
    EXPECT_EQ(p.getInt("--grid-size"), 64);

    Parser p2(nullptr, Common::NONE);
    p2.integer("--grid-size", "Grid edge in cells", 64);
    Argv spaceForm({"prog", "--grid-size", "128"});
    p2.parse(spaceForm.argc(), spaceForm.argv());
    EXPECT_EQ(p2.getInt("--grid-size"), 128);

    Parser p3(nullptr, Common::NONE);
    p3.integer("--grid-size", "Grid edge in cells", 64);
    Argv inlineForm({"prog", "--grid-size=256"});
    p3.parse(inlineForm.argc(), inlineForm.argv());
    EXPECT_EQ(p3.getInt("--grid-size"), 256);
}

TEST(IRArgsFloatTest, SpaceAndInlineFormsAndDefault) {
    Parser p(nullptr, Common::NONE);
    p.number("--zoom", "Camera zoom", 4.0f);
    Argv absent({"prog"});
    p.parse(absent.argc(), absent.argv());
    EXPECT_FLOAT_EQ(p.getFloat("--zoom"), 4.0f);

    Parser p2(nullptr, Common::NONE);
    p2.number("--zoom", "Camera zoom", 4.0f);
    Argv spaceForm({"prog", "--zoom", "2.5"});
    p2.parse(spaceForm.argc(), spaceForm.argv());
    EXPECT_FLOAT_EQ(p2.getFloat("--zoom"), 2.5f);

    Parser p3(nullptr, Common::NONE);
    p3.number("--zoom", "Camera zoom", 4.0f);
    Argv inlineForm({"prog", "--zoom=1.5"});
    p3.parse(inlineForm.argc(), inlineForm.argv());
    EXPECT_FLOAT_EQ(p3.getFloat("--zoom"), 1.5f);
}

TEST(IRArgsStringTest, SpaceAndInlineFormsAndDefault) {
    Parser p(nullptr, Common::NONE);
    p.string("--config-preset", "Path to a Lua config preset", "");
    Argv absent({"prog"});
    p.parse(absent.argc(), absent.argv());
    EXPECT_EQ(p.getString("--config-preset"), "");

    Parser p2(nullptr, Common::NONE);
    p2.string("--config-preset", "Path to a Lua config preset", "");
    Argv spaceForm({"prog", "--config-preset", "a.lua"});
    p2.parse(spaceForm.argc(), spaceForm.argv());
    EXPECT_EQ(p2.getString("--config-preset"), "a.lua");

    Parser p3(nullptr, Common::NONE);
    p3.string("--config-preset", "Path to a Lua config preset", "");
    Argv inlineForm({"prog", "--config-preset=b.lua"});
    p3.parse(inlineForm.argc(), inlineForm.argv());
    EXPECT_EQ(p3.getString("--config-preset"), "b.lua");
}

TEST(IRArgsOptionalIntTest, BareSwitchResolvesToDefault) {
    Parser p(nullptr, Common::NONE);
    p.optionalInt("--auto-screenshot", "Headless capture", kDefaultAutoScreenshotWarmup);
    Argv a({"prog", "--auto-screenshot"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.getInt("--auto-screenshot"), kDefaultAutoScreenshotWarmup);
    EXPECT_TRUE(p.wasProvided("--auto-screenshot"));
}

TEST(IRArgsOptionalIntTest, SpaceSeparatedTrailingIntIsConsumed) {
    Parser p(nullptr, Common::NONE);
    p.optionalInt("--auto-screenshot", "Headless capture", kDefaultAutoScreenshotWarmup);
    Argv a({"prog", "--auto-screenshot", "25"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.getInt("--auto-screenshot"), 25);
}

TEST(IRArgsOptionalIntTest, NonPositiveTrailingTokenIsNotConsumed) {
    Parser p(nullptr, Common::NONE);
    p.optionalInt("--auto-screenshot", "Headless capture", kDefaultAutoScreenshotWarmup);
    p.positional("next", "Next positional");
    Argv a({"prog", "--auto-screenshot", "0"});
    p.parse(a.argc(), a.argv());
    // "0" is not > 0, so it is left for the positional loop rather than
    // consumed as the trailing int.
    EXPECT_EQ(p.getInt("--auto-screenshot"), kDefaultAutoScreenshotWarmup);
    EXPECT_EQ(p.getPositional("next"), "0");
}

TEST(IRArgsOptionalIntTest, AbsentTokenLeavesWarmupFramesZero) {
    Parser p(nullptr, Common::ENGINE);
    Argv a({"prog"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.autoScreenshotWarmupFrames(), 0);
}

// ─────────────────────────────────────────────
// Positionals / standalone path
// ─────────────────────────────────────────────

TEST(IRArgsPositionalTest, FixedPositionalsReadBackByName) {
    Parser p(nullptr, Common::NONE);
    p.positional("baseline", "Baseline PNG");
    p.positional("current", "Current PNG");
    Argv a({"prog", "base.png", "cur.png"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.getPositional("baseline"), "base.png");
    EXPECT_EQ(p.getPositional("current"), "cur.png");
}

TEST(IRArgsPositionalTest, VariadicTailCapturesRemainder) {
    Parser p(nullptr, Common::NONE);
    p.positional("out", "Output file");
    p.variadic("inputs", "Input files", 1);
    Argv a({"prog", "out.png", "a.png", "b.png", "c.png"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.getPositional("out"), "out.png");
    const std::vector<std::string> &all = p.positionalArgs();
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all[0], "out.png");
    EXPECT_EQ(all[3], "c.png");
}

TEST(IRArgsPositionalDeathTest, TooFewPositionalsExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    p.positional("baseline", "Baseline PNG");
    p.positional("current", "Current PNG");
    Argv a({"prog", "base.png"});
    EXPECT_EXIT(
        p.parse(a.argc(), a.argv()),
        ::testing::ExitedWithCode(2),
        "Expected 2 positional argument"
    );
}

TEST(IRArgsPositionalDeathTest, UnexpectedExtraPositionalExitsWithCode2) {
    Parser p(nullptr, Common::NONE);
    p.positional("baseline", "Baseline PNG");
    Argv a({"prog", "base.png", "extra.png"});
    EXPECT_EXIT(
        p.parse(a.argc(), a.argv()),
        ::testing::ExitedWithCode(2),
        "Unexpected positional argument"
    );
}

// ─────────────────────────────────────────────
// --help / usage()
// ─────────────────────────────────────────────

TEST(IRArgsHelpDeathTest, HelpFlagExitsWithCode0) {
    Parser p(nullptr, Common::NONE);
    p.flag("--no-overlay", "Hide the debug overlay");
    Argv a({"prog", "--help"});
    EXPECT_EXIT(p.parse(a.argc(), a.argv()), ::testing::ExitedWithCode(0), "");
}

TEST(IRArgsUsageTest, ContainsRegisteredArgName) {
    Parser p(nullptr, Common::NONE);
    p.flag("--no-overlay", "Hide the debug overlay");
    const std::string text = p.usage();
    EXPECT_NE(text.find("--no-overlay"), std::string::npos);
}

// ─────────────────────────────────────────────
// Multi-value list — coexistence + negative values. Folds in the two
// numbers()/getFloats() cases unique to PR #2161's suite (its space/inline/
// zero-fill happy paths are already covered above, so those are dropped to
// keep this the single IRArgs suite rather than duplicating either side).
// ─────────────────────────────────────────────

TEST(IRArgsFloatListTest, CoexistsWithOtherFlags) {
    Parser p(nullptr, Common::NONE);
    p.flag("--verbose", "A flag");
    p.numbers("--xy", "x y", 2);
    p.integer("--n", "Count", 0);
    Argv a({"prog", "--verbose", "--xy", "3", "4", "--n", "9"});
    p.parse(a.argc(), a.argv());
    EXPECT_TRUE(p.getFlag("--verbose"));
    EXPECT_EQ(p.getInt("--n"), 9);
    const std::vector<float> &v = p.getFloats("--xy");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_FLOAT_EQ(v[0], 3.0f);
    EXPECT_FLOAT_EQ(v[1], 4.0f);
}

TEST(IRArgsFloatListTest, AcceptsNegativeValues) {
    Parser p(nullptr, Common::NONE);
    p.numbers("--range", "lo hi", 2);
    Argv a({"prog", "--range", "-1.5", "2.5"});
    p.parse(a.argc(), a.argv());
    const std::vector<float> &v = p.getFloats("--range");
    ASSERT_EQ(v.size(), 2u);
    EXPECT_FLOAT_EQ(v[0], -1.5f);
    EXPECT_FLOAT_EQ(v[1], 2.5f);
}

// ─────────────────────────────────────────────
// enumValue() / getEnum() — validated-enum flag (the ENUM type added by
// PR #2161; folded here so IRArgs has one unified suite)
// ─────────────────────────────────────────────

TEST(IRArgsEnumTest, AcceptsAllowedValue) {
    Parser p(nullptr, Common::NONE);
    p.enumValue("--debug-overlay", "mode", {"none", "ao", "shadow"}, "none");
    Argv a({"prog", "--debug-overlay", "shadow"});
    p.parse(a.argc(), a.argv());
    EXPECT_TRUE(p.wasProvided("--debug-overlay"));
    EXPECT_EQ(p.getEnum("--debug-overlay"), "shadow");
}

TEST(IRArgsEnumTest, OmittedReturnsDefault) {
    Parser p(nullptr, Common::NONE);
    p.enumValue("--debug-overlay", "mode", {"none", "ao", "shadow"}, "none");
    Argv a({"prog"});
    p.parse(a.argc(), a.argv());
    EXPECT_FALSE(p.wasProvided("--debug-overlay"));
    EXPECT_EQ(p.getEnum("--debug-overlay"), "none");
}

TEST(IRArgsEnumTest, InlineEqualsFormAccepted) {
    Parser p(nullptr, Common::NONE);
    p.enumValue("--mode", "mode", {"a", "b", "c"}, "a");
    Argv a({"prog", "--mode=b"});
    p.parse(a.argc(), a.argv());
    EXPECT_EQ(p.getEnum("--mode"), "b");
}

} // namespace
