#include <gtest/gtest.h>
#include <irreden/asset/json_sidecar.hpp>

#include <limits>
#include <string>

namespace {

using namespace IRAsset;

TEST(JsonSidecar, EmptyObject) {
    JsonSidecarWriter j;
    j.beginObject();
    j.endObject();
    EXPECT_EQ(j.str(), "{}");
}

TEST(JsonSidecar, EmptyArray) {
    JsonSidecarWriter j;
    j.beginArray();
    j.endArray();
    EXPECT_EQ(j.str(), "[]");
}

TEST(JsonSidecar, ControlCharactersEscapedAsUnicode) {
    // Chars not handled by named escapes but in U+0000–U+001F emit \uXXXX.
    JsonSidecarWriter j;
    j.beginObject();
    j.key("nul");
    j.valueString(std::string(1, '\x00'));  // U+0000 — null byte
    j.key("esc");
    j.valueString(std::string(1, '\x1b'));  // U+001B — ESC
    j.key("us");
    j.valueString(std::string(1, '\x1f'));  // U+001F — unit separator
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\\u0000"), std::string::npos);
    EXPECT_NE(out.find("\\u001b"), std::string::npos);
    EXPECT_NE(out.find("\\u001f"), std::string::npos);
}

TEST(JsonSidecar, NumericEdgeCases) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("uint_max");
    j.valueUInt(std::numeric_limits<std::uint64_t>::max());
    j.key("int_min");
    j.valueInt(std::numeric_limits<std::int64_t>::min());
    j.key("neg");
    j.valueInt(-1);
    j.key("pos_zero");
    j.valueFloat(+0.0);
    j.key("neg_zero");
    j.valueFloat(-0.0);
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("18446744073709551615"), std::string::npos);
    EXPECT_NE(out.find("-9223372036854775808"), std::string::npos);
    EXPECT_NE(out.find("-1"), std::string::npos);
    // Both zeros are finite: must not emit "null".
    EXPECT_EQ(out.find("\"pos_zero\": null"), std::string::npos);
    EXPECT_EQ(out.find("\"neg_zero\": null"), std::string::npos);
}

TEST(JsonSidecar, InfinityEmitsNull) {
    // Infinity is not valid JSON; like NaN, the writer emits null.
    JsonSidecarWriter j;
    j.beginObject();
    j.key("pos_inf");
    j.valueFloat(std::numeric_limits<double>::infinity());
    j.key("neg_inf");
    j.valueFloat(-std::numeric_limits<double>::infinity());
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\"pos_inf\": null"), std::string::npos);
    EXPECT_NE(out.find("\"neg_inf\": null"), std::string::npos);
    EXPECT_EQ(out.find("Infinity"), std::string::npos);
    EXPECT_EQ(out.find("infinity"), std::string::npos);
}

TEST(JsonSidecar, KeyOrderPreserved) {
    // Keys appear in insertion order — sidecars are PR-diffable.
    JsonSidecarWriter j;
    j.beginObject();
    j.key("c"); j.valueInt(3);
    j.key("a"); j.valueInt(1);
    j.key("b"); j.valueInt(2);
    j.endObject();
    const std::string out = j.str();
    const auto c_pos = out.find("\"c\"");
    const auto a_pos = out.find("\"a\"");
    const auto b_pos = out.find("\"b\"");
    ASSERT_NE(c_pos, std::string::npos);
    ASSERT_NE(a_pos, std::string::npos);
    ASSERT_NE(b_pos, std::string::npos);
    EXPECT_LT(c_pos, a_pos);
    EXPECT_LT(a_pos, b_pos);
}

TEST(JsonSidecar, WriteToFileFailsForBadPath) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("x"); j.valueInt(1);
    j.endObject();
    EXPECT_FALSE(writeJsonSidecarToFile("/nonexistent_ir_dir/ir_test_sidecar.json", j.str()));
}

} // namespace
