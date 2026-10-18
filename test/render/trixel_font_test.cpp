#include <gtest/gtest.h>

#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>

#include <algorithm>
#include <cstdint>
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

// Lit texels that are a lone lattice triangle: no lit vertical neighbour and
// no lit diamond partner, (c + 1, r) when c + r is even, else (c - 1, r).
std::vector<std::string> loneTriangles(TextSize size, char c) {
    const IRRender::GlyphMetrics metrics = IRRender::glyphMetrics(size);
    const std::span<const std::uint8_t> rows = IRRender::glyphRows(size, c);
    const auto lit = [&](int col, int row) {
        return col >= 0 && col < metrics.width_ && row >= 0 && row < metrics.height_ &&
               IRRender::glyphRowPixel(rows[row], metrics.width_, col);
    };
    std::vector<std::string> lone;
    for (int row = 0; row < metrics.height_; ++row) {
        for (int col = 0; col < metrics.width_; ++col) {
            if (!lit(col, row)) {
                continue;
            }
            const int partner = (col + row) % 2 == 0 ? col + 1 : col - 1;
            if (!lit(col, row - 1) && !lit(col, row + 1) && !lit(partner, row)) {
                lone.push_back(
                    std::string("'") + c + "' (" + std::to_string(col) + ", " +
                    std::to_string(row) + ")"
                );
            }
        }
    }
    return lone;
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

TEST(TrixelFont, NeitherSetHasALoneTriangle) {
    for (TextSize size : {TextSize::LARGE, TextSize::SMALL}) {
        for (char c : allChars()) {
            const std::vector<std::string> lone = loneTriangles(size, c);
            EXPECT_TRUE(lone.empty())
                << (size == TextSize::SMALL ? "SMALL " : "LARGE ") << testing::PrintToString(lone);
        }
    }
}

TEST(TrixelFont, NoTwoSmallCharactersShareABitmap) {
    std::vector<const IRRender::SmallGlyph *> glyphs;
    for (char c : allChars()) {
        const IRRender::SmallGlyph *glyph = IRRender::getSmallGlyph(c);
        if (std::find(glyphs.begin(), glyphs.end(), glyph) == glyphs.end()) {
            glyphs.push_back(glyph);
        }
    }
    ASSERT_EQ(glyphs.size(), 63u);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        for (std::size_t j = i + 1; j < glyphs.size(); ++j) {
            EXPECT_NE(*glyphs[i], *glyphs[j]) << "glyphs " << i << " and " << j;
        }
    }
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
