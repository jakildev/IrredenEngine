#include <gtest/gtest.h>

#include <font_maker/font_source.hpp>
#include <irreden/render/trixel_font.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using FontMaker::FontSource;

std::string readAll(const fs::path &path) {
    std::string text;
    FontMaker::detail::readFile(path, text);
    return text;
}

void writeAll(const fs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
}

struct ShippedHeader {
    const char *file_;
    const char *prefix_;
    int width_;
    int height_;
};

constexpr ShippedHeader kShipped[] = {
    {"trixel_font.hpp", "", IRRender::kGlyphWidth, IRRender::kGlyphHeight},
    {"trixel_font_small.hpp", "Small", IRRender::kSmallGlyphWidth, IRRender::kSmallGlyphHeight},
};

class FontMakerSource : public testing::Test {
  protected:
    void SetUp() override {
        const testing::TestInfo *info = testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / (std::string("font_maker_source_") + info->name());
        fs::remove_all(m_dir);
        fs::create_directories(m_dir / "irreden" / "render");
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(m_dir, error);
    }

    // A copy of a shipped header at the include path it is reached by, so a
    // compile probe resolves the LARGE header's include of the SMALL one.
    fs::path copyShipped(const char *file) {
        const fs::path target = m_dir / "irreden" / "render" / file;
        fs::copy_file(fs::path(IR_TEST_FONT_HEADER_DIR) / file, target);
        return target;
    }

    fs::path m_dir;
};

// The byte range two texts differ over: [first, end) in `a`.
std::pair<std::size_t, std::size_t> diffRange(const std::string &a, const std::string &b) {
    std::size_t first = 0;
    while (first < a.size() && first < b.size() && a[first] == b[first]) {
        ++first;
    }
    std::size_t tailA = a.size();
    std::size_t tailB = b.size();
    while (tailA > first && tailB > first && a[tailA - 1] == b[tailB - 1]) {
        --tailA;
        --tailB;
    }
    return {first, tailA};
}

TEST_F(FontMakerSource, ParsesBothShippedHeaders) {
    for (const ShippedHeader &shipped : kShipped) {
        FontSource source;
        ASSERT_EQ(FontMaker::loadFontSource(copyShipped(shipped.file_), source), "")
            << shipped.file_;
        EXPECT_EQ(source.prefix_, shipped.prefix_);
        EXPECT_EQ(source.width_, shipped.width_);
        EXPECT_EQ(source.height_, shipped.height_);
        ASSERT_EQ(source.glyphs_.size(), 63u) << shipped.file_;
        for (const FontMaker::GlyphLiteral &glyph : source.glyphs_) {
            EXPECT_EQ(glyph.malformed_, "") << glyph.name_;
        }
        EXPECT_EQ(source.glyphFor('a'), source.glyphFor('A'));
        EXPECT_EQ(source.glyphs_[source.glyphFor('\\')].name_, "Backslash");
        EXPECT_EQ(source.glyphs_[source.glyphFor('\'')].name_, "Apostrophe");
        EXPECT_EQ(source.glyphs_[source.glyphFor('"')].name_, "Quote");
        EXPECT_EQ(source.glyphs_[source.fallbackGlyph_].name_, "Question");
        EXPECT_EQ(source.glyphFor('`'), -1);
    }
}

TEST_F(FontMakerSource, ParsedBitmapsMatchTheCompiledSets) {
    for (const ShippedHeader &shipped : kShipped) {
        FontSource source;
        ASSERT_EQ(FontMaker::loadFontSource(copyShipped(shipped.file_), source), "");
        const IRRender::TextSize size = source.ruleSet();
        EXPECT_EQ(size, shipped.prefix_[0] ? IRRender::TextSize::SMALL : IRRender::TextSize::LARGE);
        for (int ascii = 32; ascii < 127; ++ascii) {
            const int glyph = source.glyphFor(static_cast<char>(ascii));
            const std::span<const std::uint8_t> compiled =
                IRRender::glyphRows(size, static_cast<char>(ascii));
            const std::vector<std::uint8_t> &parsed =
                source.glyphs_[glyph >= 0 ? glyph : source.fallbackGlyph_].rows_;
            EXPECT_TRUE(std::equal(compiled.begin(), compiled.end(), parsed.begin(), parsed.end()))
                << shipped.file_ << " ascii " << ascii;
        }
    }
}

TEST_F(FontMakerSource, SaveRewritesOnlyTheEditedQuotedRow) {
    for (const ShippedHeader &shipped : kShipped) {
        const fs::path path = copyShipped(shipped.file_);
        const std::string before = readAll(path);
        FontSource source;
        ASSERT_EQ(FontMaker::loadFontSource(path, source), "");
        const int glyph = source.glyphFor('K');
        std::vector<std::uint8_t> rows = source.glyphs_[glyph].rows_;
        const int row = 3;
        rows[row] ^= static_cast<std::uint8_t>(1u << (source.width_ - 1));
        const std::size_t rowOffset = source.glyphs_[glyph].rowOffsets_[row];

        ASSERT_EQ(FontMaker::saveGlyph(source, glyph, rows), "") << shipped.file_;
        const std::string after = readAll(path);
        ASSERT_EQ(after.size(), before.size());
        const auto [first, end] = diffRange(before, after);
        EXPECT_EQ(first, rowOffset) << "only column 0 of the row flips";
        EXPECT_EQ(end, rowOffset + 1);
        EXPECT_EQ(after, source.text_);

        FontSource reopened;
        ASSERT_EQ(FontMaker::loadFontSource(path, reopened), "");
        EXPECT_EQ(reopened.glyphs_[glyph].rows_, rows);
        for (std::size_t other = 0; other < reopened.glyphs_.size(); ++other) {
            if (static_cast<int>(other) != glyph) {
                EXPECT_EQ(reopened.glyphs_[other].rows_, source.glyphs_[other].rows_);
            }
        }
    }
}

TEST_F(FontMakerSource, SaveRefusesAHeaderChangedOnDisk) {
    const fs::path path = copyShipped("trixel_font.hpp");
    FontSource source;
    ASSERT_EQ(FontMaker::loadFontSource(path, source), "");
    const std::string edited = readAll(path) + "// edited elsewhere\n";
    writeAll(path, edited);

    std::vector<std::uint8_t> rows = source.glyphs_[0].rows_;
    rows[0] ^= 1u;
    EXPECT_NE(FontMaker::saveGlyph(source, 0, rows), "");
    EXPECT_EQ(readAll(path), edited);
}

TEST_F(FontMakerSource, SaveRefusesAMalformedOrMissingLiteral) {
    const fs::path path = copyShipped("trixel_font_small.hpp");
    std::string text = readAll(path);
    const std::size_t literal = text.find("kSmallGlyph_B = makeSmallGlyph(");
    ASSERT_NE(literal, std::string::npos);
    const std::size_t firstRow = text.find('"', literal);
    const std::size_t rowEnd = text.find('\n', firstRow);
    text.erase(firstRow, rowEnd + 1 - firstRow);
    const std::size_t caseC = text.find("case 'C': case 'c': return &kSmallGlyph_C;");
    ASSERT_NE(caseC, std::string::npos);
    text.replace(caseC, 42, "case 'C': case 'c': return &kSmallGlyph_Cx;");
    writeAll(path, text);

    FontSource source;
    ASSERT_EQ(FontMaker::loadFontSource(path, source), "");
    const int malformed = source.glyphFor('B');
    ASSERT_GE(malformed, 0);
    EXPECT_NE(source.glyphs_[malformed].malformed_, "");
    EXPECT_EQ(source.glyphFor('C'), -1) << "a case naming no literal reads as Missing";

    const std::vector<std::uint8_t> rows(source.height_, 0xFu);
    EXPECT_NE(FontMaker::saveGlyph(source, malformed, rows), "");
    EXPECT_NE(FontMaker::saveGlyph(source, source.glyphFor('C'), rows), "");
    EXPECT_EQ(readAll(path), text);
}

TEST_F(FontMakerSource, RejectsAHeaderOutsideTheSchema) {
    const fs::path path = m_dir / "not_a_font.hpp";
    writeAll(path, "#pragma once\nconstexpr int kAnswer = 42;\n");
    FontSource source;
    EXPECT_NE(FontMaker::loadFontSource(path, source), "");
}

TEST_F(FontMakerSource, SavedHeadersCompile) {
    const fs::path large = copyShipped("trixel_font.hpp");
    const fs::path small = copyShipped("trixel_font_small.hpp");
    for (const fs::path &path : {large, small}) {
        FontSource source;
        ASSERT_EQ(FontMaker::loadFontSource(path, source), "");
        const int glyph = source.glyphFor('7');
        std::vector<std::uint8_t> rows = source.glyphs_[glyph].rows_;
        rows.back() ^= 1u;
        ASSERT_EQ(FontMaker::saveGlyph(source, glyph, rows), "");
    }
    const fs::path probe = m_dir / "probe.cpp";
    writeAll(
        probe,
        "#include <irreden/render/trixel_font.hpp>\n"
        "static_assert(IRRender::getGlyph('7') != &IRRender::kGlyph_Question);\n"
        "static_assert(IRRender::getSmallGlyph('7') != &IRRender::kSmallGlyph_Question);\n"
        "int main() { return 0; }\n"
    );
    const std::string command = std::string("\"") + IR_TEST_CXX_COMPILER +
                                "\" -std=c++20 -fsyntax-only -I\"" + m_dir.string() + "\" \"" +
                                probe.string() + "\"";
    EXPECT_EQ(std::system(command.c_str()), 0) << command;
}

} // namespace
