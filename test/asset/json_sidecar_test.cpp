#include <gtest/gtest.h>

#include <irreden/asset/json_sidecar.hpp>

#include <cstdint>
#include <limits>
#include <string>

namespace {

using namespace IRAsset;

TEST(JsonSidecar, EmptyObjectProducesEmptyBraces) {
    JsonSidecarWriter j;
    j.beginObject();
    j.endObject();
    EXPECT_EQ(j.str(), "{}");
}

TEST(JsonSidecar, EmptyArrayProducesEmptyBrackets) {
    JsonSidecarWriter j;
    j.beginArray();
    j.endArray();
    EXPECT_EQ(j.str(), "[]");
}

TEST(JsonSidecar, ObjectInsideArrayInsideObject) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("items");
    j.beginArray();
    j.beginObject();
    j.key("x");
    j.valueInt(7);
    j.endObject();
    j.endArray();
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\"items\""), std::string::npos);
    EXPECT_NE(out.find("\"x\": 7"), std::string::npos);
    EXPECT_EQ(out.front(), '{');
    EXPECT_EQ(out.back(), '}');
}

TEST(JsonSidecar, StringEscapingCarriageReturnBackspaceFormfeed) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("cr");
    j.valueString("\r");
    j.key("bs");
    j.valueString("\b");
    j.key("ff");
    j.valueString("\f");
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\\r"), std::string::npos);
    EXPECT_NE(out.find("\\b"), std::string::npos);
    EXPECT_NE(out.find("\\f"), std::string::npos);
}

TEST(JsonSidecar, StringEscapingRawControlCharacters) {
    // U+0001, U+001E, U+001F fall through to the \u00XX path.
    JsonSidecarWriter j;
    j.beginObject();
    j.key("ctrl");
    j.valueString("\x01\x1e\x1f");
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\\u0001"), std::string::npos);
    EXPECT_NE(out.find("\\u001e"), std::string::npos);
    EXPECT_NE(out.find("\\u001f"), std::string::npos);
}

TEST(JsonSidecar, StringEscapingNullByte) {
    // U+0000 (null byte) must produce the 6-char escape \u0000, not silent truncation.
    JsonSidecarWriter j;
    j.beginObject();
    j.key("nul");
    j.valueString(std::string("\x00", 1));
    j.endObject();
    const std::string out = j.str();
    EXPECT_NE(out.find("\\u0000"), std::string::npos);
}

TEST(JsonSidecar, Int64MinRoundTrip) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("min");
    j.valueInt(std::numeric_limits<int64_t>::min());
    j.endObject();
    EXPECT_NE(j.str().find("-9223372036854775808"), std::string::npos);
}

TEST(JsonSidecar, Uint64MaxRoundTrip) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("max");
    j.valueUInt(std::numeric_limits<uint64_t>::max());
    j.endObject();
    EXPECT_NE(j.str().find("18446744073709551615"), std::string::npos);
}

TEST(JsonSidecar, PositiveAndNegativeZeroAreFiniteNotNull) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("pz");
    j.valueFloat(+0.0);
    j.key("nz");
    j.valueFloat(-0.0);
    j.endObject();
    const std::string out = j.str();
    EXPECT_EQ(out.find("\"pz\": null"), std::string::npos);
    EXPECT_EQ(out.find("\"nz\": null"), std::string::npos);
}

TEST(JsonSidecar, InfinityEmitsNull) {
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
}

TEST(JsonSidecar, KeyOrderPreserved) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("alpha"); j.valueInt(1);
    j.key("beta");  j.valueInt(2);
    j.key("gamma"); j.valueInt(3);
    j.endObject();
    const std::string out = j.str();
    const auto posA = out.find("\"alpha\"");
    const auto posB = out.find("\"beta\"");
    const auto posG = out.find("\"gamma\"");
    ASSERT_NE(posA, std::string::npos);
    ASSERT_NE(posB, std::string::npos);
    ASSERT_NE(posG, std::string::npos);
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posG);
}

TEST(JsonSidecar, WriteToFileFailsOnBadPath) {
    JsonSidecarWriter j;
    j.beginObject();
    j.key("x");
    j.valueInt(1);
    j.endObject();
    EXPECT_FALSE(writeJsonSidecarToFile("/nonexistent_dir_irreden/sidecar.json", j.str()));
}

} // namespace
