#include <gtest/gtest.h>

#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_font_validation.hpp>
#include <irreden/render/trixel_text.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <span>
#include <string>
#include <vector>

namespace {

using IRRender::TextSize;

static_assert(IRRender::kSmallGlyphStepX == 6);
static_assert(IRRender::kSmallGlyphStepY == 10);
static_assert(IRRender::kSmallGlyphStepX % 2 == 0 && IRRender::kSmallGlyphStepY % 2 == 0);
static_assert(IRRender::glyphMetrics(TextSize::LARGE).stepX_ == IRRender::kGlyphStepX);
static_assert(IRRender::glyphMetrics(TextSize::SMALL).stepY_ == IRRender::kSmallGlyphStepY);

// Every char the 128-entry font buffer holds.
std::vector<char> allChars() {
    std::vector<char> chars;
    for (int ascii = 0; ascii < 128; ++ascii) {
        chars.push_back(static_cast<char>(ascii));
    }
    return chars;
}

bool largeHasGlyph(char c) {
    return c == '?' || IRRender::getGlyph(c) != &IRRender::kGlyph_Question;
}

bool smallHasGlyph(char c) {
    return c == '?' || IRRender::getSmallGlyph(c) != &IRRender::kSmallGlyph_Question;
}

IRRender::GlyphBitmap shippedBitmap(TextSize size, char c) {
    const IRRender::GlyphMetrics metrics = IRRender::glyphMetrics(size);
    return {metrics.width_, metrics.height_, IRRender::glyphRows(size, c)};
}

// One bitmap per distinct glyph the set's 128-entry table reaches.
std::vector<IRRender::GlyphBitmap> uniqueShippedBitmaps(TextSize size) {
    std::vector<const std::uint8_t *> seen;
    std::vector<IRRender::GlyphBitmap> bitmaps;
    for (char c : allChars()) {
        const IRRender::GlyphBitmap bitmap = shippedBitmap(size, c);
        if (std::find(seen.begin(), seen.end(), bitmap.rows_.data()) == seen.end()) {
            seen.push_back(bitmap.rows_.data());
            bitmaps.push_back(bitmap);
        }
    }
    return bitmaps;
}

std::vector<std::string> describe(char c, const std::vector<IRRender::GlyphTexelFinding> &found) {
    std::vector<std::string> out;
    for (const IRRender::GlyphTexelFinding &finding : found) {
        out.push_back(
            std::string("'") + c + "' " + IRRender::glyphRuleName(finding.rule_) + " (" +
            std::to_string(finding.col_) + ", " + std::to_string(finding.row_) + ")"
        );
    }
    return out;
}

bool hasFinding(
    const std::vector<IRRender::GlyphTexelFinding> &found,
    IRRender::GlyphRule rule,
    int col,
    int row
) {
    return std::any_of(found.begin(), found.end(), [&](const IRRender::GlyphTexelFinding &f) {
        return f.rule_ == rule && f.col_ == col && f.row_ == row;
    });
}

TEST(TrixelFont, SmallSetCoversExactlyTheLargeSetsCharacters) {
    int covered = 0;
    for (char c : allChars()) {
        EXPECT_EQ(largeHasGlyph(c), smallHasGlyph(c))
            << "character " << static_cast<int>(c) << " ('" << c << "')";
        covered += largeHasGlyph(c) ? 1 : 0;
    }
    EXPECT_EQ(covered, 63 + 26) << "63 glyphs, 26 of them also reached by a lowercase letter";
}

TEST(TrixelFont, NeitherSetBreaksAGlyphRule) {
    for (TextSize size : {TextSize::LARGE, TextSize::SMALL}) {
        for (char c : allChars()) {
            const std::vector<std::string> found =
                describe(c, IRRender::glyphTexelFindings(size, shippedBitmap(size, c)));
            EXPECT_TRUE(found.empty())
                << (size == TextSize::SMALL ? "SMALL " : "LARGE ") << testing::PrintToString(found);
        }
    }
}

TEST(TrixelFont, NeitherSetHasTwoCharactersSharingABitmap) {
    for (TextSize size : {TextSize::LARGE, TextSize::SMALL}) {
        const std::vector<IRRender::GlyphBitmap> bitmaps = uniqueShippedBitmaps(size);
        ASSERT_EQ(bitmaps.size(), 63u);
        EXPECT_TRUE(IRRender::duplicateBitmapPairs(bitmaps).empty())
            << (size == TextSize::SMALL ? "SMALL" : "LARGE");
    }
}

TEST(TrixelFont, LoneTriangleFiresOnAnIsolatedTexel) {
    constexpr IRRender::Glyph rows = IRRender::makeGlyph(
        "......"
        "......"
        "......"
        "......"
        "......"
        "..X..."
        "......"
        "......"
        "......"
        "......"
        "......"
    );
    const auto found = IRRender::glyphTexelFindings(TextSize::LARGE, {6, 11, rows});
    ASSERT_EQ(found.size(), 1u) << testing::PrintToString(describe('?', found));
    EXPECT_TRUE(hasFinding(found, IRRender::GlyphRule::LONE_TRIANGLE, 2, 5));
}

TEST(TrixelFont, LargeWrongParityFiresOnlyOnTheNotchedStemEnd) {
    // Columns 0-1 over rows 1-6: the row-1 end is a notch, the row-6 end a point.
    constexpr IRRender::Glyph rows = IRRender::makeGlyph(
        "......"
        "XX...."
        "XX...."
        "XX...."
        "XX...."
        "XX...."
        "XX...."
        "......"
        "......"
        "......"
        "......"
    );
    const auto found = IRRender::glyphTexelFindings(TextSize::LARGE, {6, 11, rows});
    ASSERT_EQ(found.size(), 1u) << testing::PrintToString(describe('?', found));
    EXPECT_TRUE(hasFinding(found, IRRender::GlyphRule::WRONG_PARITY, 0, 1));
}

TEST(TrixelFont, SmallWrongParityFiresOnAnOddRowOuterTexel) {
    constexpr IRRender::SmallGlyph rows = IRRender::makeSmallGlyph(
        "...."
        "X..."
        "...."
        "...."
        "...."
        "...."
        "...."
    );
    const auto found = IRRender::glyphTexelFindings(TextSize::SMALL, {4, 7, rows});
    EXPECT_TRUE(hasFinding(found, IRRender::GlyphRule::WRONG_PARITY, 0, 1))
        << testing::PrintToString(describe('?', found));
}

TEST(TrixelFont, DuplicateBitmapFiresOnACopiedGlyphButNotOnTwoEmptyOnes) {
    const IRRender::GlyphBitmap a = shippedBitmap(TextSize::SMALL, 'A');
    const IRRender::GlyphBitmap b = shippedBitmap(TextSize::SMALL, 'B');
    const IRRender::GlyphBitmap space = shippedBitmap(TextSize::SMALL, ' ');
    const std::vector<IRRender::GlyphBitmap> glyphs = {a, b, space, a, space};
    const auto pairs = IRRender::duplicateBitmapPairs(glyphs);
    ASSERT_EQ(pairs.size(), 1u);
    EXPECT_EQ(pairs[0], (std::pair<std::size_t, std::size_t>(0, 3)));
}

TEST(TrixelFont, FontBufferHoldsTheLargeBlockThenTheSmallBlock) {
    namespace GuiText = IRPrefab::GuiText;
    const std::vector<std::uint32_t> buffer = GuiText::buildFontBuffer();
    ASSERT_EQ(buffer.size(), static_cast<std::size_t>(128 * 11 + 128 * 7));
    for (int ascii = 0; ascii < 128; ++ascii) {
        const char c = static_cast<char>(ascii);
        const IRRender::Glyph &large = *IRRender::getGlyph(c);
        for (int row = 0; row < IRRender::kGlyphHeight; ++row) {
            EXPECT_EQ(buffer[ascii * 11 + row], large[row]) << "LARGE " << ascii;
        }
        const IRRender::SmallGlyph &small = *IRRender::getSmallGlyph(c);
        for (int row = 0; row < IRRender::kSmallGlyphHeight; ++row) {
            EXPECT_EQ(buffer[128 * 11 + ascii * 7 + row], small[row]) << "SMALL " << ascii;
        }
    }
}

std::vector<IRRender::GlyphDrawCommand> layOut(const std::string &text, TextSize size) {
    std::vector<IRRender::GlyphDrawCommand> commands;
    IRPrefab::GuiText::expandTextToCommands(
        commands,
        text,
        IRMath::ivec2(0, 0),
        IRMath::ivec2(64, 64),
        IRMath::IRColors::kWhite,
        0,
        IRComponents::TextAlignH::LEFT,
        IRComponents::TextAlignV::TOP,
        0,
        0,
        1,
        size
    );
    return commands;
}

int commandX(const IRRender::GlyphDrawCommand &command) {
    return static_cast<int>(command.positionPacked & 0xFFFFu);
}

IRMath::ivec2 commandCell(const IRRender::GlyphDrawCommand &command) {
    return IRMath::ivec2(
        static_cast<int>(
            (command.styleFlags >> IRRender::kGlyphStyleCellWidthShift) &
            IRRender::kGlyphStyleCellMask
        ),
        static_cast<int>(
            (command.styleFlags >> IRRender::kGlyphStyleCellHeightShift) &
            IRRender::kGlyphStyleCellMask
        )
    );
}

TEST(TrixelFont, CommandsNameTheirSetsRowsAndCell) {
    const auto small = layOut("AB", TextSize::SMALL);
    ASSERT_EQ(small.size(), 2u);
    EXPECT_EQ(commandX(small[1]) - commandX(small[0]), 6);
    EXPECT_EQ(small[0].firstFontRow, static_cast<std::uint32_t>(128 * 11 + 'A' * 7));
    EXPECT_EQ(small[1].firstFontRow, static_cast<std::uint32_t>(128 * 11 + 'B' * 7));
    EXPECT_EQ(commandCell(small[0]), IRMath::ivec2(4, 7));
    EXPECT_EQ(small[0].styleFlags & 0xFFu, 1u);

    const auto large = layOut("AB", TextSize::LARGE);
    ASSERT_EQ(large.size(), 2u);
    EXPECT_EQ(commandX(large[1]) - commandX(large[0]), 8);
    EXPECT_EQ(large[0].firstFontRow, static_cast<std::uint32_t>('A' * 11));
    EXPECT_EQ(commandCell(large[0]), IRMath::ivec2(6, 11));
    EXPECT_EQ(large[0].styleFlags & 0x1FFu, 1u) << "the lattice bit is stamped at dispatch";
}

TEST(TrixelFont, MeasurementsFollowTheSize) {
    EXPECT_EQ(IRPrefab::GuiText::textRunWidth("ABCD", 1, TextSize::SMALL), 22);
    EXPECT_EQ(IRPrefab::GuiText::textRunWidth("ABCD", 1, TextSize::LARGE), 30);
    EXPECT_EQ(IRPrefab::GuiText::textRunWidth("ABCD", 1), 30);
    EXPECT_EQ(IRRender::measureText("AB\nCD", 0, TextSize::SMALL), IRMath::ivec2(10, 17));
    EXPECT_EQ(IRRender::measureText("AB\nCD", 0, TextSize::LARGE), IRMath::ivec2(14, 25));
    EXPECT_EQ(IRRender::measureText("AB\nCD"), IRMath::ivec2(14, 25));
}

} // namespace
