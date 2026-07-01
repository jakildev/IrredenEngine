#include <gtest/gtest.h>
#include <irreden/ir_args.hpp>

#include <string>
#include <vector>

// Coverage for the fixed-arity float-list (FLOAT_LIST) and validated-enum (ENUM)
// IRArgs features. FLOAT_LIST shipped (#2143) with no unit tests, so these fill
// that gap alongside the new ENUM type. The reject / --help paths call
// std::exit, so they can't run in-process under gtest; these exercise the happy
// paths + omitted-arg defaults, which is what the canvas_stress migration (and
// any future consumer) relies on.

namespace {

using namespace IRArgs;

// Parser::parse skips argv[0] (program path) and may advance the index, but does
// not write through argv, so backing the pointers with a std::string vector is
// safe. std::string::data() is non-const since C++17.
std::vector<char *> makeArgv(std::vector<std::string> &storage) {
    std::vector<char *> argv;
    argv.reserve(storage.size());
    for (std::string &s : storage) {
        argv.push_back(s.data());
    }
    return argv;
}

TEST(IRArgsNumbersTest, ParsesFixedArityFloatList) {
    Parser parser("test", Common::NONE);
    parser.numbers("--sweep-yaw", "from to count", 3);
    std::vector<std::string> args{"prog", "--sweep-yaw", "0.0", "6.28", "17"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_TRUE(parser.wasProvided("--sweep-yaw"));
    const std::vector<float> &values = parser.getFloats("--sweep-yaw");
    ASSERT_EQ(values.size(), 3u);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    EXPECT_FLOAT_EQ(values[1], 6.28f);
    EXPECT_FLOAT_EQ(values[2], 17.0f);
    // Counts/indices represented as floats stay exact for these magnitudes.
    EXPECT_EQ(static_cast<int>(values[2]), 17);
}

TEST(IRArgsNumbersTest, AcceptsInlineCommaSeparatedList) {
    Parser parser("test", Common::NONE);
    parser.numbers("--sweep-yaw", "from to count", 3);
    std::vector<std::string> args{"prog", "--sweep-yaw=0,360,8"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_TRUE(parser.wasProvided("--sweep-yaw"));
    const std::vector<float> &values = parser.getFloats("--sweep-yaw");
    ASSERT_EQ(values.size(), 3u);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    EXPECT_FLOAT_EQ(values[1], 360.0f);
    EXPECT_FLOAT_EQ(values[2], 8.0f);
}

TEST(IRArgsNumbersTest, OmittedListIsZeroFilled) {
    Parser parser("test", Common::NONE);
    parser.numbers("--sweep-frames", "count settle", 2);
    std::vector<std::string> args{"prog"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    // getFloats returns the registered all-zero defaults (sized to the arity),
    // not an empty vector, when the arg was omitted.
    EXPECT_FALSE(parser.wasProvided("--sweep-frames"));
    const std::vector<float> &values = parser.getFloats("--sweep-frames");
    ASSERT_EQ(values.size(), 2u);
    EXPECT_FLOAT_EQ(values[0], 0.0f);
    EXPECT_FLOAT_EQ(values[1], 0.0f);
}

TEST(IRArgsNumbersTest, ListCoexistsWithOtherFlags) {
    Parser parser("test", Common::NONE);
    parser.flag("--verbose", "a flag");
    parser.numbers("--xy", "x y", 2);
    parser.integer("--n", "count", 0);
    std::vector<std::string> args{"prog", "--verbose", "--xy", "3", "4", "--n", "9"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_TRUE(parser.getFlag("--verbose"));
    EXPECT_EQ(parser.getInt("--n"), 9);
    const std::vector<float> &values = parser.getFloats("--xy");
    ASSERT_EQ(values.size(), 2u);
    EXPECT_FLOAT_EQ(values[0], 3.0f);
    EXPECT_FLOAT_EQ(values[1], 4.0f);
}

TEST(IRArgsNumbersTest, ListAcceptsNegativeValues) {
    Parser parser("test", Common::NONE);
    parser.numbers("--range", "lo hi", 2);
    std::vector<std::string> args{"prog", "--range", "-1.5", "2.5"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    const std::vector<float> &values = parser.getFloats("--range");
    ASSERT_EQ(values.size(), 2u);
    EXPECT_FLOAT_EQ(values[0], -1.5f);
    EXPECT_FLOAT_EQ(values[1], 2.5f);
}

TEST(IRArgsEnumTest, AcceptsAllowedValue) {
    Parser parser("test", Common::NONE);
    parser.enumValue("--debug-overlay", "mode", {"none", "ao", "shadow"}, "none");
    std::vector<std::string> args{"prog", "--debug-overlay", "shadow"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_TRUE(parser.wasProvided("--debug-overlay"));
    EXPECT_EQ(parser.getEnum("--debug-overlay"), "shadow");
}

TEST(IRArgsEnumTest, OmittedReturnsDefault) {
    Parser parser("test", Common::NONE);
    parser.enumValue("--debug-overlay", "mode", {"none", "ao", "shadow"}, "none");
    std::vector<std::string> args{"prog"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_FALSE(parser.wasProvided("--debug-overlay"));
    EXPECT_EQ(parser.getEnum("--debug-overlay"), "none");
}

TEST(IRArgsEnumTest, InlineEqualsValueAccepted) {
    Parser parser("test", Common::NONE);
    parser.enumValue("--mode", "mode", {"a", "b", "c"}, "a");
    std::vector<std::string> args{"prog", "--mode=b"};
    std::vector<char *> argv = makeArgv(args);
    parser.parse(static_cast<int>(argv.size()), argv.data());

    EXPECT_EQ(parser.getEnum("--mode"), "b");
}

} // namespace
