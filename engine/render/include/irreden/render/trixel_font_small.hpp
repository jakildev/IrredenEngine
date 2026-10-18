#ifndef TRIXEL_FONT_SMALL_H
#define TRIXEL_FONT_SMALL_H

#include <array>
#include <cstdint>

namespace IRRender {

// The small glyph set: a 4x7 cell on the same lattice display as the large
// set in trixel_font.hpp, covering exactly its characters. Texel (c, r) of a
// parity-aligned origin is a left-pointing triangle when c + r is even, a
// right-pointing one when odd, each spanning rows r-1..r+1. At this size the
// art follows stricter rules:
//   - bodies use columns 0-3 so the design mirrors about x = 2, a lattice
//     mirror axis; a 1-wide column of texels is a solid strip;
//   - a horizontal stroke is a 1-row bar or a 2-row ribbon. A full-width bar
//     sits on an even row, where its texels pair into diamonds; on an odd row
//     only columns 1-2 pair, so columns 0 and 3 of an odd-row bar must sit on
//     a vertical stroke. Two 1-row bars need two clear rows between them
//     (rows 0, 3, 6) or their diamond tips touch;
//   - no lone triangle: every lit texel has a lit vertical neighbour or its
//     diamond partner, (c + 1, r) when c + r is even, else (c - 1, r);
//   - no two characters share a bitmap.
// Two columns of spacing keep the step even, which keeps every glyph origin
// on the same parity; three rows of line spacing cover the triangles that
// reach one row past the bitmap.
constexpr int kSmallGlyphWidth = 4;
constexpr int kSmallGlyphHeight = 7;
constexpr int kSmallGlyphSpacingX = 2;
constexpr int kSmallGlyphSpacingY = 3;
constexpr int kSmallGlyphStepX = kSmallGlyphWidth + kSmallGlyphSpacingX;  // 6
constexpr int kSmallGlyphStepY = kSmallGlyphHeight + kSmallGlyphSpacingY; // 10
static_assert(kSmallGlyphStepX % 2 == 0 && kSmallGlyphStepY % 2 == 0, "parity-safe step");

using SmallGlyph = std::array<uint8_t, kSmallGlyphHeight>;

namespace detail {

// Packs one row of `width` chars ('X' or '#' lit) into a byte, column 0 in
// bit (width - 1).
constexpr uint8_t packGlyphRow(const char *row, int width) {
    uint8_t rowBits = 0;
    for (int col = 0; col < width; ++col) {
        if (row[col] == 'X' || row[col] == '#') {
            rowBits |= static_cast<uint8_t>(1 << (width - 1 - col));
        }
    }
    return rowBits;
}

// Packs `Height` rows of `Width` chars into one byte per row. Both glyph sets
// build their bitmaps with it.
template <int Width, int Height>
constexpr std::array<uint8_t, Height> packGlyphPattern(const char *pattern) {
    std::array<uint8_t, Height> rows{};
    for (int row = 0; row < Height; ++row) {
        rows[row] = packGlyphRow(pattern + row * Width, Width);
    }
    return rows;
}

} // namespace detail

// kSmallGlyphHeight rows of kSmallGlyphWidth chars; 'X' or '#' is lit.
constexpr SmallGlyph makeSmallGlyph(const char *pattern) {
    return detail::packGlyphPattern<kSmallGlyphWidth, kSmallGlyphHeight>(pattern);
}

constexpr SmallGlyph kSmallGlyph_A = makeSmallGlyph(
    ".XX."
    "XXXX"
    "X..X"
    "X..X"
    "XXXX"
    "X..X"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_B = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "XXX."
    "X..X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_C = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "X..."
    "X..."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_D = makeSmallGlyph(
    "XX.."
    "XXX."
    "X.XX"
    "X..X"
    "X.XX"
    "XXX."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_E = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "XXX."
    "X..."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_F = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "XXX."
    "X..."
    "X..."
    "X..."
);

constexpr SmallGlyph kSmallGlyph_G = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "X..."
    "X.XX"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_H = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
    "X..X"
    "X..X"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_I = makeSmallGlyph(
    "XXXX"
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_J = makeSmallGlyph(
    "..XX"
    "...X"
    "...X"
    "...X"
    "...X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_K = makeSmallGlyph(
    "X.XX"
    "XXX."
    "XX.."
    "X..."
    "XX.."
    "XXX."
    "X.XX"
);

constexpr SmallGlyph kSmallGlyph_L = makeSmallGlyph(
    "X..."
    "X..."
    "X..."
    "X..."
    "X..."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_M = makeSmallGlyph(
    "X..X"
    "XXXX"
    "XXXX"
    "XXXX"
    "X..X"
    "X..X"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_N = makeSmallGlyph(
    "X..X"
    "XX.X"
    "XX.X"
    "X.XX"
    "X.XX"
    "X..X"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_O = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_P = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "XXXX"
    "X..."
    "X..."
    "X..."
);

constexpr SmallGlyph kSmallGlyph_Q = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "X..X"
    "X.XX"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_R = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "XXXX"
    "XX.."
    "X.X."
    "X.XX"
);

constexpr SmallGlyph kSmallGlyph_S = makeSmallGlyph(
    "XXXX"
    "X..."
    "XX.."
    ".XX."
    "..XX"
    "...X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_T = makeSmallGlyph(
    "XXXX"
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_U = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_V = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_W = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
    "XXXX"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_X = makeSmallGlyph(
    "X..X"
    "X..X"
    "XXXX"
    ".XX."
    "XXXX"
    "X..X"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_Y = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    ".XX."
    ".XX."
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_Z = makeSmallGlyph(
    "XXXX"
    "...X"
    "..XX"
    ".XX."
    "XX.."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_0 = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X.XX"
    "XXXX"
    "XX.X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_1 = makeSmallGlyph(
    ".XX."
    "XXX."
    "X.X."
    "..X."
    "..X."
    "..X."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_2 = makeSmallGlyph(
    "XXXX"
    "...X"
    "...X"
    "XXXX"
    "X..."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_3 = makeSmallGlyph(
    "XXXX"
    "...X"
    "...X"
    ".XXX"
    "...X"
    "...X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_4 = makeSmallGlyph(
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
    "...X"
    "...X"
    "...X"
);

constexpr SmallGlyph kSmallGlyph_5 = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "XXX."
    "...X"
    "...X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_6 = makeSmallGlyph(
    "XXXX"
    "X..."
    "X..."
    "XXXX"
    "X..X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_7 = makeSmallGlyph(
    "XXXX"
    "...X"
    "..XX"
    ".XX."
    ".XX."
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_8 = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    ".XX."
    "X..X"
    "X..X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_9 = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X..X"
    "XXXX"
    "...X"
    "...X"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_Space = makeSmallGlyph(
    "...."
    "...."
    "...."
    "...."
    "...."
    "...."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Period = makeSmallGlyph(
    "...."
    "...."
    "...."
    "...."
    "...."
    ".XX."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Comma = makeSmallGlyph(
    "...."
    "...."
    "...."
    "...."
    "...."
    ".XX."
    ".X.."
);

constexpr SmallGlyph kSmallGlyph_Colon = makeSmallGlyph(
    "...."
    ".XX."
    "...."
    "...."
    "...."
    ".XX."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Semicolon = makeSmallGlyph(
    "...."
    ".XX."
    "...."
    "...."
    "...."
    ".XX."
    ".X.."
);

constexpr SmallGlyph kSmallGlyph_Exclamation = makeSmallGlyph(
    ".XX."
    ".XX."
    ".XX."
    "...."
    "...."
    ".XX."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Question = makeSmallGlyph(
    "XXXX"
    "...X"
    "..XX"
    ".XX."
    "...."
    ".XX."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Dash = makeSmallGlyph(
    "...."
    "...."
    "...."
    "...."
    "XXXX"
    "...."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Underscore = makeSmallGlyph(
    "...."
    "...."
    "...."
    "...."
    "...."
    "...."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_Slash = makeSmallGlyph(
    "..XX"
    "..XX"
    ".XX."
    ".XX."
    ".XX."
    "XX.."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_Backslash = makeSmallGlyph(
    "XX.."
    "XX.."
    ".XX."
    ".XX."
    ".XX."
    "..XX"
    "..XX"
);

constexpr SmallGlyph kSmallGlyph_ParenOpen = makeSmallGlyph(
    "..XX"
    ".X.."
    ".X.."
    ".X.."
    ".X.."
    ".X.."
    "..XX"
);

constexpr SmallGlyph kSmallGlyph_ParenClose = makeSmallGlyph(
    "XX.."
    "..X."
    "..X."
    "..X."
    "..X."
    "..X."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_BracketOpen = makeSmallGlyph(
    "XX.."
    "X..."
    "X..."
    "X..."
    "X..."
    "X..."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_BracketClose = makeSmallGlyph(
    "..XX"
    "...X"
    "...X"
    "...X"
    "...X"
    "...X"
    "..XX"
);

constexpr SmallGlyph kSmallGlyph_Equals = makeSmallGlyph(
    "...."
    "XXXX"
    "XXXX"
    "...."
    "...."
    "XXXX"
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_Plus = makeSmallGlyph(
    "...."
    ".XX."
    ".XX."
    "XXXX"
    "XXXX"
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_At = makeSmallGlyph(
    "XXXX"
    "X..X"
    "X.XX"
    "X.XX"
    "X..."
    "X..."
    "XXXX"
);

constexpr SmallGlyph kSmallGlyph_Hash = makeSmallGlyph(
    "X..X"
    "XXXX"
    "X..X"
    "X..X"
    "X..X"
    "XXXX"
    "X..X"
);

constexpr SmallGlyph kSmallGlyph_Asterisk = makeSmallGlyph(
    "...."
    ".XX."
    "XXXX"
    ".XX."
    "XXXX"
    ".XX."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Percent = makeSmallGlyph(
    "..XX"
    "...."
    "...."
    ".XX."
    "...."
    "...."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_Pipe = makeSmallGlyph(
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
    ".XX."
);

constexpr SmallGlyph kSmallGlyph_LessThan = makeSmallGlyph(
    "..XX"
    ".XX."
    "XX.."
    "X..."
    "XX.."
    ".XX."
    "..XX"
);

constexpr SmallGlyph kSmallGlyph_GreaterThan = makeSmallGlyph(
    "XX.."
    ".XX."
    "..XX"
    "...X"
    "..XX"
    ".XX."
    "XX.."
);

constexpr SmallGlyph kSmallGlyph_Tilde = makeSmallGlyph(
    "...."
    "...."
    "XX.."
    "XXXX"
    "..XX"
    "...."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Apostrophe = makeSmallGlyph(
    ".XX."
    ".XX."
    "...."
    "...."
    "...."
    "...."
    "...."
);

constexpr SmallGlyph kSmallGlyph_Quote = makeSmallGlyph(
    "X..X"
    "X..X"
    "...."
    "...."
    "...."
    "...."
    "...."
);

// Lowercase folds to uppercase; a character the set lacks draws as '?'.
// clang-format off
constexpr const SmallGlyph *getSmallGlyph(char c) {
    switch (c) {
    case 'A': case 'a': return &kSmallGlyph_A;
    case 'B': case 'b': return &kSmallGlyph_B;
    case 'C': case 'c': return &kSmallGlyph_C;
    case 'D': case 'd': return &kSmallGlyph_D;
    case 'E': case 'e': return &kSmallGlyph_E;
    case 'F': case 'f': return &kSmallGlyph_F;
    case 'G': case 'g': return &kSmallGlyph_G;
    case 'H': case 'h': return &kSmallGlyph_H;
    case 'I': case 'i': return &kSmallGlyph_I;
    case 'J': case 'j': return &kSmallGlyph_J;
    case 'K': case 'k': return &kSmallGlyph_K;
    case 'L': case 'l': return &kSmallGlyph_L;
    case 'M': case 'm': return &kSmallGlyph_M;
    case 'N': case 'n': return &kSmallGlyph_N;
    case 'O': case 'o': return &kSmallGlyph_O;
    case 'P': case 'p': return &kSmallGlyph_P;
    case 'Q': case 'q': return &kSmallGlyph_Q;
    case 'R': case 'r': return &kSmallGlyph_R;
    case 'S': case 's': return &kSmallGlyph_S;
    case 'T': case 't': return &kSmallGlyph_T;
    case 'U': case 'u': return &kSmallGlyph_U;
    case 'V': case 'v': return &kSmallGlyph_V;
    case 'W': case 'w': return &kSmallGlyph_W;
    case 'X': case 'x': return &kSmallGlyph_X;
    case 'Y': case 'y': return &kSmallGlyph_Y;
    case 'Z': case 'z': return &kSmallGlyph_Z;
    case '0': return &kSmallGlyph_0;
    case '1': return &kSmallGlyph_1;
    case '2': return &kSmallGlyph_2;
    case '3': return &kSmallGlyph_3;
    case '4': return &kSmallGlyph_4;
    case '5': return &kSmallGlyph_5;
    case '6': return &kSmallGlyph_6;
    case '7': return &kSmallGlyph_7;
    case '8': return &kSmallGlyph_8;
    case '9': return &kSmallGlyph_9;
    case ' ': return &kSmallGlyph_Space;
    case '.': return &kSmallGlyph_Period;
    case ',': return &kSmallGlyph_Comma;
    case ':': return &kSmallGlyph_Colon;
    case ';': return &kSmallGlyph_Semicolon;
    case '!': return &kSmallGlyph_Exclamation;
    case '?': return &kSmallGlyph_Question;
    case '-': return &kSmallGlyph_Dash;
    case '_': return &kSmallGlyph_Underscore;
    case '/': return &kSmallGlyph_Slash;
    case '\\': return &kSmallGlyph_Backslash;
    case '(': return &kSmallGlyph_ParenOpen;
    case ')': return &kSmallGlyph_ParenClose;
    case '[': return &kSmallGlyph_BracketOpen;
    case ']': return &kSmallGlyph_BracketClose;
    case '=': return &kSmallGlyph_Equals;
    case '+': return &kSmallGlyph_Plus;
    case '@': return &kSmallGlyph_At;
    case '#': return &kSmallGlyph_Hash;
    case '*': return &kSmallGlyph_Asterisk;
    case '%': return &kSmallGlyph_Percent;
    case '|': return &kSmallGlyph_Pipe;
    case '<': return &kSmallGlyph_LessThan;
    case '>': return &kSmallGlyph_GreaterThan;
    case '~': return &kSmallGlyph_Tilde;
    case '\'': return &kSmallGlyph_Apostrophe;
    case '"': return &kSmallGlyph_Quote;
    default:  return &kSmallGlyph_Question;
    }
}
// clang-format on

} // namespace IRRender

#endif /* TRIXEL_FONT_SMALL_H */
