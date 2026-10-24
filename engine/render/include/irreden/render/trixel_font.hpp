#ifndef TRIXEL_FONT_H
#define TRIXEL_FONT_H

#include <irreden/render/trixel_font_small.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace IRRender {

// Glyphs are drawn for the GUI canvas's lattice display, where texel (c, r)
// of a parity-aligned text origin is a left-pointing triangle when c + r is
// even and a right-pointing one when odd, each one texel wide and spanning
// rows r-1..r+1. The art relies on that mapping:
//   - a 2-wide stem in columns (0,1), (2,3) or (4,5) is solid, and is pointed
//     at both ends when it starts and ends on even rows (0, 2, ..., 10); a
//     stem starting at an odd column or row ends in a notch instead;
//   - a 2-row bar is a solid zigzag ribbon; a single row of texels is a chain
//     of diamonds touching only at their tips, so it reads as a dotted line;
//   - a diagonal is a chain of diamonds (texels (c, r) + (c+1, r) with c + r
//     even) stepping one column per row, or 3-row stem segments stepping one
//     column per three rows for the steeper angle;
//   - an isolated texel is a lone triangle: a spur or a bite.
// Bodies use columns 0-5 so the design mirrors about x = 3, a lattice mirror
// axis; the six columns plus two of spacing keep the step even, which keeps
// every glyph origin on the same parity. Row 0's triangles reach one row
// above the bitmap and row 10's one row below, which the line spacing covers.
constexpr int kGlyphWidth = 6;
constexpr int kGlyphHeight = 11;
constexpr int kGlyphSpacingX = 2;
constexpr int kGlyphSpacingY = 3;
constexpr int kGlyphStepX = kGlyphWidth + kGlyphSpacingX;   // 8, even -> parity-safe
constexpr int kGlyphStepY = kGlyphHeight + kGlyphSpacingY;  // 14, even -> parity-safe

using Glyph = std::array<uint8_t, kGlyphHeight>;

// Converts a visual string pattern into a packed Glyph at compile time.
// Pattern is kGlyphHeight rows of kGlyphWidth chars (66 chars total).
// 'X' or '#' = filled trixel, anything else = empty.
// Adjacent string literals auto-concatenate: "row1" "row2" "row3" ...
constexpr Glyph makeGlyph(const char* pattern) {
    return detail::packGlyphPattern<kGlyphWidth, kGlyphHeight>(pattern);
}

// --- Letters A-Z ---

constexpr Glyph kGlyph_A = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXXX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
);

constexpr Glyph kGlyph_B = makeGlyph(
    "XXXX.."
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXXX"
);

constexpr Glyph kGlyph_C = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_D = makeGlyph(
    "XXXX.."
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXX."
    "XXXX.."
);

constexpr Glyph kGlyph_E = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "XX...."
    "XX...."
    "XXXXX."
    "XXXXX."
    "XX...."
    "XX...."
    "XX...."
    "XXXXXX"
    "XXXXXX"
);

constexpr Glyph kGlyph_F = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "XX...."
    "XX...."
    "XXXXX."
    "XXXXX."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
);

constexpr Glyph kGlyph_G = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX...."
    "XX...."
    "XX...."
    "XX.XXX"
    "XX.XXX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_H = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXXX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
);

constexpr Glyph kGlyph_I = makeGlyph(
    ".XXXX."
    ".XXXX."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    ".XXXX."
    ".XXXX."
);

constexpr Glyph kGlyph_J = makeGlyph(
    "..XXXX"
    "..XXXX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_K = makeGlyph(
    "XX..XX"
    "XX.XX."
    "XXXX.."
    "XXX..."
    "XX...."
    "XXX..."
    "XXXX.."
    "XX.XX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
);

constexpr Glyph kGlyph_L = makeGlyph(
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XXXXXX"
    "XXXXXX"
);

constexpr Glyph kGlyph_M = makeGlyph(
    "XX..XX"
    "XXXXXX"
    "XXXXXX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
);

constexpr Glyph kGlyph_N = makeGlyph(
    "XX..XX"
    "XXX.XX"
    "XXX.XX"
    "XXX.XX"
    "XXXXXX"
    "XXXXXX"
    "XXXXXX"
    "XX.XXX"
    "XX.XXX"
    "XX.XXX"
    "XX..XX"
);

constexpr Glyph kGlyph_O = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_P = makeGlyph(
    "XXXX.."
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXX."
    "XX...."
    "XX...."
    "XX...."
);

constexpr Glyph kGlyph_Q = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XXXX"
);

constexpr Glyph kGlyph_R = makeGlyph(
    "XXXX.."
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXX."
    "XXXX.."
    "XX.XX."
    "XX..XX"
);

constexpr Glyph kGlyph_S = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX...."
    "XXXX.."
    ".XXXX."
    "....XX"
    "....XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_T = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
);

constexpr Glyph kGlyph_U = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_V = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    ".XXXX."
    ".XXXX."
    "..XX.."
    "..XX.."
    "..XX.."
);

constexpr Glyph kGlyph_W = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XXXXXX"
    "XX..XX"
);

constexpr Glyph kGlyph_X = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
    "..XX.."
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
);

constexpr Glyph kGlyph_Y = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
);

constexpr Glyph kGlyph_Z = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "....XX"
    "....XX"
    "....XX"
    "...XX."
    "...XX."
    "...XX."
    "..XX.."
    "XXXXXX"
    "XXXXXX"
);

// --- Digits 0-9 ---

constexpr Glyph kGlyph_0 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_1 = makeGlyph(
    "..XX.."
    ".XXX.."
    "XXXX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    ".XXXX."
    ".XXXX."
);

constexpr Glyph kGlyph_2 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "....XX"
    "....XX"
    "...XX."
    "..XX.."
    ".XX..."
    "XX...."
    "XXXXXX"
    "XXXXXX"
);

constexpr Glyph kGlyph_3 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "....XX"
    "..XXXX"
    "..XXX."
    "....XX"
    "....XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_4 = makeGlyph(
    "..XXXX"
    "..XXXX"
    "..XXXX"
    ".XX.XX"
    ".XX.XX"
    ".XX.XX"
    "XXXXXX"
    ".XXXXX"
    "....XX"
    "....XX"
    "....XX"
);

constexpr Glyph kGlyph_5 = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "XX...."
    "XX...."
    "XXXX.."
    ".XXXX."
    "....XX"
    "....XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_6 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX...."
    "XXXX.."
    "XXXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_7 = makeGlyph(
    "XXXXXX"
    "XXXXXX"
    "....XX"
    "....XX"
    "....XX"
    "...XX."
    "...XX."
    "...XX."
    "..XX.."
    "..XX.."
    "..XX.."
);

constexpr Glyph kGlyph_8 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_9 = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XX..XX"
    ".XXXXX"
    "..XXXX"
    "....XX"
    "XX..XX"
    ".XXXX."
    "..XX.."
);

// --- Symbols ---

constexpr Glyph kGlyph_Space = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
);

constexpr Glyph kGlyph_Period = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "..XX.."
);

constexpr Glyph kGlyph_Comma = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "..XX.."
    "..X..."
    "..X..."
);

constexpr Glyph kGlyph_Colon = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "..XX.."
    "......"
    "......"
    "......"
    "......"
    "......"
    "..XX.."
);

constexpr Glyph kGlyph_Semicolon = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "..XX.."
    "......"
    "......"
    "......"
    "..XX.."
    "..X..."
    "..X..."
);

constexpr Glyph kGlyph_Exclamation = makeGlyph(
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "......"
    "......"
    "......"
    "..XX.."
);

constexpr Glyph kGlyph_Question = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "....XX"
    "....XX"
    "...XX."
    "..XX.."
    "......"
    "......"
    "......"
    "..XX.."
);

constexpr Glyph kGlyph_Dash = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    ".XXXX."
    ".XXXX."
    "......"
    "......"
    "......"
    "......"
    "......"
);

constexpr Glyph kGlyph_Underscore = makeGlyph(
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "XXXXXX"
    "XXXXXX"
);

constexpr Glyph kGlyph_Slash = makeGlyph(
    "....XX"
    "...XX."
    "...XX."
    "...XX."
    "..XX.."
    "..XX.."
    "..XX.."
    ".XX..."
    ".XX..."
    ".XX..."
    "XX...."
);

constexpr Glyph kGlyph_Backslash = makeGlyph(
    "XX...."
    ".XX..."
    ".XX..."
    ".XX..."
    "..XX.."
    "..XX.."
    "..XX.."
    "...XX."
    "...XX."
    "...XX."
    "....XX"
);

constexpr Glyph kGlyph_ParenOpen = makeGlyph(
    "..XX.."
    ".XX..."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    ".XX..."
    "..XX.."
);

constexpr Glyph kGlyph_ParenClose = makeGlyph(
    "..XX.."
    "...XX."
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "...XX."
    "..XX.."
);

constexpr Glyph kGlyph_BracketOpen = makeGlyph(
    "XXXX.."
    "XXXX.."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XX...."
    "XXXX.."
    "XXXX.."
);

constexpr Glyph kGlyph_BracketClose = makeGlyph(
    "..XXXX"
    "..XXXX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "....XX"
    "..XXXX"
    "..XXXX"
);

constexpr Glyph kGlyph_Equals = makeGlyph(
    "......"
    "......"
    "......"
    "XXXXXX"
    "XXXXXX"
    "......"
    "......"
    "XXXXXX"
    "XXXXXX"
    "......"
    "......"
);

constexpr Glyph kGlyph_Plus = makeGlyph(
    "......"
    "......"
    "..XX.."
    "..XX.."
    "XXXXXX"
    "XXXXXX"
    "..XX.."
    "..XX.."
    "..XX.."
    "......"
    "......"
);

constexpr Glyph kGlyph_At = makeGlyph(
    "..XX.."
    ".XXXX."
    "XX..XX"
    "XX..XX"
    "XXXXXX"
    "XX..XX"
    "XX..XX"
    "XX...."
    "XX...."
    ".XXXX."
    "..XX.."
);

constexpr Glyph kGlyph_Hash = makeGlyph(
    ".X..X."
    ".X..X."
    ".X..X."
    "XXXXXX"
    "XXXXXX"
    ".X..X."
    ".X..X."
    "XXXXXX"
    "XXXXXX"
    ".X..X."
    ".X..X."
);

constexpr Glyph kGlyph_Asterisk = makeGlyph(
    "......"
    "......"
    "..XX.."
    ".XXXX."
    "..XX.."
    ".XXXX."
    "..XX.."
    "......"
    "......"
    "......"
    "......"
);

constexpr Glyph kGlyph_Percent = makeGlyph(
    "....XX"
    "....XX"
    "....XX"
    "......"
    "..XX.."
    "..XX.."
    "..XX.."
    "......"
    "XX...."
    "XX...."
    "XX...."
);

constexpr Glyph kGlyph_Pipe = makeGlyph(
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
    "..XX.."
);

constexpr Glyph kGlyph_LessThan = makeGlyph(
    "......"
    "......"
    "....XX"
    "...XX."
    "..XX.."
    ".XX..."
    "..XX.."
    "...XX."
    "....XX"
    "......"
    "......"
);

constexpr Glyph kGlyph_GreaterThan = makeGlyph(
    "......"
    "......"
    "XX...."
    ".XX..."
    "..XX.."
    "...XX."
    "..XX.."
    ".XX..."
    "XX...."
    "......"
    "......"
);

constexpr Glyph kGlyph_Tilde = makeGlyph(
    "......"
    "......"
    "......"
    ".XX..."
    "XXXXXX"
    "...XX."
    "......"
    "......"
    "......"
    "......"
    "......"
);

constexpr Glyph kGlyph_Apostrophe = makeGlyph(
    "..XX.."
    "..XX.."
    "..XX.."
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
);

constexpr Glyph kGlyph_Quote = makeGlyph(
    "XX..XX"
    "XX..XX"
    "XX..XX"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
    "......"
);

constexpr const Glyph *getGlyph(char c) {
    switch (c) {
    case 'A': case 'a': return &kGlyph_A;
    case 'B': case 'b': return &kGlyph_B;
    case 'C': case 'c': return &kGlyph_C;
    case 'D': case 'd': return &kGlyph_D;
    case 'E': case 'e': return &kGlyph_E;
    case 'F': case 'f': return &kGlyph_F;
    case 'G': case 'g': return &kGlyph_G;
    case 'H': case 'h': return &kGlyph_H;
    case 'I': case 'i': return &kGlyph_I;
    case 'J': case 'j': return &kGlyph_J;
    case 'K': case 'k': return &kGlyph_K;
    case 'L': case 'l': return &kGlyph_L;
    case 'M': case 'm': return &kGlyph_M;
    case 'N': case 'n': return &kGlyph_N;
    case 'O': case 'o': return &kGlyph_O;
    case 'P': case 'p': return &kGlyph_P;
    case 'Q': case 'q': return &kGlyph_Q;
    case 'R': case 'r': return &kGlyph_R;
    case 'S': case 's': return &kGlyph_S;
    case 'T': case 't': return &kGlyph_T;
    case 'U': case 'u': return &kGlyph_U;
    case 'V': case 'v': return &kGlyph_V;
    case 'W': case 'w': return &kGlyph_W;
    case 'X': case 'x': return &kGlyph_X;
    case 'Y': case 'y': return &kGlyph_Y;
    case 'Z': case 'z': return &kGlyph_Z;
    case '0': return &kGlyph_0;
    case '1': return &kGlyph_1;
    case '2': return &kGlyph_2;
    case '3': return &kGlyph_3;
    case '4': return &kGlyph_4;
    case '5': return &kGlyph_5;
    case '6': return &kGlyph_6;
    case '7': return &kGlyph_7;
    case '8': return &kGlyph_8;
    case '9': return &kGlyph_9;
    case ' ': return &kGlyph_Space;
    case '.': return &kGlyph_Period;
    case ',': return &kGlyph_Comma;
    case ':': return &kGlyph_Colon;
    case ';': return &kGlyph_Semicolon;
    case '!': return &kGlyph_Exclamation;
    case '?': return &kGlyph_Question;
    case '-': return &kGlyph_Dash;
    case '_': return &kGlyph_Underscore;
    case '/': return &kGlyph_Slash;
    case '\\': return &kGlyph_Backslash;
    case '(': return &kGlyph_ParenOpen;
    case ')': return &kGlyph_ParenClose;
    case '[': return &kGlyph_BracketOpen;
    case ']': return &kGlyph_BracketClose;
    case '=': return &kGlyph_Equals;
    case '+': return &kGlyph_Plus;
    case '@': return &kGlyph_At;
    case '#': return &kGlyph_Hash;
    case '*': return &kGlyph_Asterisk;
    case '%': return &kGlyph_Percent;
    case '|': return &kGlyph_Pipe;
    case '<': return &kGlyph_LessThan;
    case '>': return &kGlyph_GreaterThan;
    case '~': return &kGlyph_Tilde;
    case '\'': return &kGlyph_Apostrophe;
    case '"': return &kGlyph_Quote;
    default:  return &kGlyph_Question;
    }
}

constexpr bool glyphRowPixel(uint8_t rowBits, int width, int col) {
    return (rowBits >> (width - 1 - col)) & 1;
}

constexpr bool glyphPixel(const Glyph &glyph, int col, int row) {
    return glyphRowPixel(glyph[row], kGlyphWidth, col);
}

// Which glyph set a text draw uses. LARGE is this header's 6x11 set, SMALL the
// 4x7 set in trixel_font_small.hpp; both cover the same characters.
enum class TextSize : int { LARGE = 0, SMALL = 1 };

// Cell, spacing and step of one glyph set, in texels at font size 1.
struct GlyphMetrics {
    int width_;
    int height_;
    int spacingX_;
    int spacingY_;
    int stepX_;
    int stepY_;
};

constexpr GlyphMetrics glyphMetrics(TextSize size) {
    if (size == TextSize::SMALL) {
        return {
            kSmallGlyphWidth,
            kSmallGlyphHeight,
            kSmallGlyphSpacingX,
            kSmallGlyphSpacingY,
            kSmallGlyphStepX,
            kSmallGlyphStepY
        };
    }
    return {kGlyphWidth, kGlyphHeight, kGlyphSpacingX, kGlyphSpacingY, kGlyphStepX, kGlyphStepY};
}

// The bitmap rows of `c` in `size`'s set, top row first; glyphRowPixel reads
// column `col` with width glyphMetrics(size).width_.
constexpr std::span<const uint8_t> glyphRows(TextSize size, char c) {
    if (size == TextSize::SMALL) {
        return *getSmallGlyph(c);
    }
    return *getGlyph(c);
}

} // namespace IRRender

#endif /* TRIXEL_FONT_H */
