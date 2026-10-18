#include <gtest/gtest.h>

#include <irreden/asset/json_reader.hpp>

#include <clocale>
#include <cstdint>
#include <string>

namespace {

using namespace IRAsset;

JsonValue parseOk(std::string_view text) {
    auto result = parseJson(text);
    if (!result) {
        ADD_FAILURE() << "line " << result.error().line_ << " col " << result.error().column_
                      << ": " << result.error().message_;
        return {};
    }
    return *result;
}

void expectError(
    std::string_view text,
    std::uint32_t line,
    std::uint32_t column,
    std::string_view messageFragment
) {
    auto result = parseJson(text);
    ASSERT_FALSE(result.has_value()) << "parsed: " << text;
    EXPECT_EQ(result.error().line_, line) << result.error().message_;
    EXPECT_EQ(result.error().column_, column) << result.error().message_;
    EXPECT_NE(result.error().message_.find(messageFragment), std::string::npos)
        << "message: " << result.error().message_;
}

TEST(JsonReaderTest, ParsesScalars) {
    EXPECT_TRUE(parseOk("null").isNull());
    EXPECT_EQ(parseOk("true").asBool(), true);
    EXPECT_EQ(parseOk("false").asBool(), false);
    EXPECT_EQ(parseOk("0").asNumber(), 0.0);
    EXPECT_EQ(parseOk("-12").asInt(), -12);
    EXPECT_EQ(parseOk("3.25").asNumber(), 3.25);
    EXPECT_EQ(parseOk("-1.5e3").asNumber(), -1500.0);
    EXPECT_EQ(parseOk("2E-2").asNumber(), 0.02);
    EXPECT_EQ(*parseOk("\"hi\"").asString(), "hi");
}

TEST(JsonReaderTest, AccessorsReturnEmptyOnTypeMismatch) {
    const JsonValue number = parseOk("7");
    EXPECT_FALSE(number.asBool().has_value());
    EXPECT_EQ(number.asString(), nullptr);
    EXPECT_EQ(number.asArray(), nullptr);
    EXPECT_EQ(number.asObject(), nullptr);
    EXPECT_EQ(number.find("x"), nullptr);
    EXPECT_FALSE(parseOk("\"7\"").asNumber().has_value());
}

TEST(JsonReaderTest, AsIntRequiresAnIntegralInRangeNumber) {
    EXPECT_EQ(parseOk("4.0").asInt(), 4);
    EXPECT_FALSE(parseOk("4.5").asInt().has_value());
    EXPECT_FALSE(parseOk("1e300").asInt().has_value());
    EXPECT_EQ(parseOk("-9223372036854775808").asInt(), INT64_MIN);
    EXPECT_FALSE(parseOk("9223372036854775808").asInt().has_value());
}

TEST(JsonReaderTest, ObjectPreservesKeyOrder) {
    const JsonValue value = parseOk(R"({"b": 1, "a": 2, "c": 3})");
    const JsonObject *members = value.asObject();
    ASSERT_NE(members, nullptr);
    ASSERT_EQ(members->size(), 3u);
    EXPECT_EQ((*members)[0].key_, "b");
    EXPECT_EQ((*members)[1].key_, "a");
    EXPECT_EQ((*members)[2].key_, "c");
    EXPECT_EQ(value.find("a")->asInt(), 2);
    EXPECT_EQ(value.find("missing"), nullptr);
}

TEST(JsonReaderTest, DuplicateKeyFindReturnsTheLast) {
    const JsonValue value = parseOk(R"({"k": 1, "k": 2})");
    ASSERT_EQ(value.asObject()->size(), 2u);
    EXPECT_EQ(value.find("k")->asInt(), 2);
}

TEST(JsonReaderTest, NestedContainersAndEmptyContainers) {
    const JsonValue value = parseOk(R"({"a": [1, [2, {}], []], "o": {"x": null}})");
    const JsonArray *a = value.find("a")->asArray();
    ASSERT_NE(a, nullptr);
    ASSERT_EQ(a->size(), 3u);
    EXPECT_EQ((*a)[0].asInt(), 1);
    EXPECT_EQ((*(*a)[1].asArray())[0].asInt(), 2);
    EXPECT_TRUE((*(*a)[1].asArray())[1].asObject()->empty());
    EXPECT_TRUE((*a)[2].asArray()->empty());
    EXPECT_TRUE(value.find("o")->find("x")->isNull());
}

TEST(JsonReaderTest, SimpleEscapesDecode) {
    const JsonValue value = parseOk(R"("q\" b\\ s\/ \b\f\n\r\t")");
    EXPECT_EQ(*value.asString(), "q\" b\\ s/ \b\f\n\r\t");
}

TEST(JsonReaderTest, UnicodeEscapesDecodeToUtf8) {
    EXPECT_EQ(*parseOk(R"("A")").asString(), "A");
    EXPECT_EQ(*parseOk(R"("é")").asString(), "\xC3\xA9");
    EXPECT_EQ(*parseOk(R"("€")").asString(), "\xE2\x82\xAC");
    EXPECT_EQ(*parseOk(R"("😀")").asString(), "\xF0\x9F\x98\x80");
    EXPECT_EQ(parseOk(R"("a\u0000b")").asString()->size(), 3u);
}

TEST(JsonReaderTest, RawUtf8PassesThrough) {
    EXPECT_EQ(*parseOk("\"caf\xC3\xA9\"").asString(), "caf\xC3\xA9");
}

TEST(JsonReaderTest, ValuesRecordLineAndColumn) {
    const JsonValue value = parseOk("{\n  \"a\": [10,\n\t20],\r\n  \"b\": true\n}");
    EXPECT_EQ(value.line(), 1u);
    EXPECT_EQ(value.column(), 1u);
    const JsonValue &a = *value.find("a");
    EXPECT_EQ(a.line(), 2u);
    EXPECT_EQ(a.column(), 8u);
    EXPECT_EQ((*a.asArray())[0].column(), 9u);
    EXPECT_EQ((*a.asArray())[1].line(), 3u);
    EXPECT_EQ((*a.asArray())[1].column(), 2u);
    EXPECT_EQ(value.find("b")->line(), 4u);
    EXPECT_EQ(value.find("b")->column(), 8u);
}

TEST(JsonReaderTest, LeadingByteOrderMarkIsSkipped) {
    const JsonValue value = parseOk("\xEF\xBB\xBF{\"a\": 1}");
    EXPECT_EQ(value.find("a")->asInt(), 1);
    EXPECT_EQ(value.column(), 1u);
}

TEST(JsonReaderTest, NumbersIgnoreTheCLocale) {
    const char *previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved = previous != nullptr ? previous : "C";
    if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr &&
        std::setlocale(LC_NUMERIC, "de_DE") == nullptr) {
        GTEST_SKIP() << "no comma-decimal locale installed";
    }
    const double parsed = parseOk("1.5").asNumber().value_or(0.0);
    std::setlocale(LC_NUMERIC, saved.c_str());
    EXPECT_EQ(parsed, 1.5);
}

TEST(JsonReaderTest, LargeFlatArray) {
    constexpr std::size_t kTargetBytes = 1u << 20;
    std::string text = "[";
    text.reserve(kTargetBytes + 8);
    std::size_t count = 0;
    while (text.size() < kTargetBytes - 2) {
        text.append(count == 0 ? "7" : ",7");
        ++count;
    }
    text.push_back(']');
    ASSERT_GE(text.size(), kTargetBytes - 2);

    const JsonValue value = parseOk(text);
    const JsonArray *elements = value.asArray();
    ASSERT_NE(elements, nullptr);
    EXPECT_EQ(elements->size(), count);
    EXPECT_EQ(elements->back().asInt(), 7);
}

TEST(JsonReaderTest, NestingBeyondTheLimitFailsWithoutOverflow) {
    const std::string atLimit = std::string(kMaxJsonDepth, '[') + std::string(kMaxJsonDepth, ']');
    EXPECT_TRUE(parseJson(atLimit).has_value());

    const std::string deep(100000, '[');
    expectError(deep, 1, kMaxJsonDepth + 1, "nesting deeper than");
}

TEST(JsonReaderTest, UnterminatedString) {
    expectError("{\n  \"name\": \"abc", 2, 11, "unterminated string");
    expectError("\"ab\\", 1, 1, "unterminated string");
}

TEST(JsonReaderTest, TrailingComma) {
    expectError("[1, 2, ]", 1, 8, "trailing comma in array");
    expectError("{\n  \"a\": 1,\n}", 3, 1, "trailing comma in object");
}

TEST(JsonReaderTest, BadEscape) {
    expectError(R"(["ok", "a\qb"])", 1, 10, "invalid escape");
    expectError(R"("\u12G4")", 1, 2, "invalid hex digit");
    expectError(R"("\u12")", 1, 2, "truncated");
}

TEST(JsonReaderTest, LoneSurrogate) {
    expectError(R"("x\ud83d")", 1, 3, "lone high surrogate");
    expectError(R"("x\ud83dA")", 1, 3, "lone high surrogate");
    expectError(R"("x\ude00")", 1, 3, "lone low surrogate");
}

TEST(JsonReaderTest, UnescapedControlCharacter) {
    expectError("\"a\nb\"", 1, 3, "unescaped control character");
    expectError("\"a\tb\"", 1, 3, "unescaped control character");
}

TEST(JsonReaderTest, InvalidNumbers) {
    expectError("01", 1, 1, "leading zero");
    expectError("[1.]", 1, 2, "no digits after '.'");
    expectError("1e+", 1, 1, "no digits in exponent");
    expectError("-", 1, 1, "invalid number");
    expectError("+1", 1, 1, "unexpected character '+'");
    expectError(".5", 1, 1, "unexpected character '.'");
    expectError("1e999", 1, 1, "out of range");
}

TEST(JsonReaderTest, InvalidLiteralsAndStructure) {
    expectError("tru", 1, 1, "invalid literal");
    expectError("[nul]", 1, 2, "invalid literal");
    expectError("", 1, 1, "unexpected end of input");
    expectError("[1 2]", 1, 4, "expected ',' or ']'");
    expectError(R"({"a" 1})", 1, 6, "expected ':'");
    expectError(R"({a: 1})", 1, 2, "expected a string key");
    expectError(R"({"a": 1 "b": 2})", 1, 9, "expected ',' or '}'");
    expectError("[1, 2", 1, 6, "unexpected end of input");
    expectError("{} {}", 1, 4, "unexpected content after the root value");
    expectError("// comment\n{}", 1, 1, "unexpected character '/'");
}

} // namespace
