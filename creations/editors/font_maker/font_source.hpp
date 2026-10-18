#ifndef FONT_MAKER_FONT_SOURCE_H
#define FONT_MAKER_FONT_SOURCE_H

#include <irreden/render/trixel_font.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// A trixel-font header as source text: its glyph constants, every
// `k<P>Glyph_<Name> = make<P>Glyph("row" "row" ...)` literal with the byte span
// of each quoted row, and the `get<P>Glyph(char)` switch that maps characters
// to literals. Saving rewrites only the quoted rows of one literal.
namespace FontMaker {

constexpr int kMaxGlyphWidth = 8;

struct GlyphLiteral {
    std::string name_;
    // Byte offset and length of each row's text inside its quotes.
    std::vector<std::size_t> rowOffsets_;
    std::vector<std::size_t> rowLengths_;
    std::vector<std::uint8_t> rows_;
    // Empty when the literal has the cell's shape; otherwise why it does not.
    std::string malformed_;
};

struct FontSource {
    std::filesystem::path path_;
    std::string prefix_;
    int width_ = 0;
    int height_ = 0;
    int spacingX_ = 0;
    int spacingY_ = 0;
    std::string text_;
    std::vector<GlyphLiteral> glyphs_;
    // Literal index per ASCII character, -1 when the switch has no case for it.
    std::array<int, 128> charGlyph_{};
    int fallbackGlyph_ = -1;

    IRRender::TextSize ruleSet() const {
        return prefix_ == "Small" || (prefix_ != "" && width_ <= 4) ? IRRender::TextSize::SMALL
                                                                    : IRRender::TextSize::LARGE;
    }

    int glyphFor(char c) const {
        const int ascii = static_cast<unsigned char>(c);
        return ascii < 128 ? charGlyph_[ascii] : -1;
    }
};

namespace detail {

inline bool isIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

inline std::size_t skipSpaceAndComments(std::string_view text, std::size_t at) {
    while (at < text.size()) {
        if (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r') {
            ++at;
        } else if (text.compare(at, 2, "//") == 0) {
            const std::size_t end = text.find('\n', at);
            at = end == std::string_view::npos ? text.size() : end;
        } else if (text.compare(at, 2, "/*") == 0) {
            const std::size_t end = text.find("*/", at + 2);
            at = end == std::string_view::npos ? text.size() : end + 2;
        } else {
            break;
        }
    }
    return at;
}

inline std::optional<int> readConstant(std::string_view text, const std::string &name) {
    const std::string needle = "constexpr int " + name + " = ";
    const std::size_t at = text.find(needle);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t digit = at + needle.size();
    int value = 0;
    bool any = false;
    while (digit < text.size() && text[digit] >= '0' && text[digit] <= '9') {
        value = value * 10 + (text[digit] - '0');
        any = true;
        ++digit;
    }
    return any ? std::optional<int>(value) : std::nullopt;
}

// The character a `case '<x>':` literal names, or nullopt for a form outside
// the one-character and simple-escape set the font headers use.
inline std::optional<char> readCharLiteral(std::string_view text, std::size_t &at) {
    if (at >= text.size() || text[at] != '\'') {
        return std::nullopt;
    }
    ++at;
    char value = 0;
    if (at < text.size() && text[at] == '\\') {
        if (at + 1 >= text.size()) {
            return std::nullopt;
        }
        switch (text[at + 1]) {
        case '\\':
            value = '\\';
            break;
        case '\'':
            value = '\'';
            break;
        case '"':
            value = '"';
            break;
        case 'n':
            value = '\n';
            break;
        case 't':
            value = '\t';
            break;
        case '0':
            value = '\0';
            break;
        default:
            return std::nullopt;
        }
        at += 2;
    } else if (at < text.size()) {
        value = text[at];
        ++at;
    }
    if (at >= text.size() || text[at] != '\'') {
        return std::nullopt;
    }
    ++at;
    return value;
}

// The row's bits; a row shorter than `width` reads its missing columns unlit.
inline std::uint8_t packedRow(std::string_view row, int width) {
    char padded[kMaxGlyphWidth + 1] = {};
    for (int col = 0; col < width; ++col) {
        padded[col] = col < static_cast<int>(row.size()) ? row[col] : '.';
    }
    return IRRender::detail::packGlyphRow(padded, width);
}

inline bool readFile(const std::filesystem::path &path, std::string &out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

} // namespace detail

inline int findGlyph(const FontSource &source, std::string_view name) {
    for (std::size_t i = 0; i < source.glyphs_.size(); ++i) {
        if (source.glyphs_[i].name_ == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Parses `text` as a trixel-font header. Returns an empty string on success,
// else why the header does not follow the schema.
inline std::string parseFontSource(std::string text, FontSource &out) {
    out.text_ = std::move(text);
    out.glyphs_.clear();
    out.charGlyph_.fill(-1);
    out.fallbackGlyph_ = -1;
    const std::string_view src = out.text_;

    const std::size_t first = src.find("Glyph_");
    const std::size_t kAt = first == std::string_view::npos ? first : src.rfind('k', first);
    if (kAt == std::string_view::npos) {
        return "no k<Prefix>Glyph_<Name> literal";
    }
    out.prefix_ = std::string(src.substr(kAt + 1, first - kAt - 1));
    const std::string &prefix = out.prefix_;
    const std::optional<int> width = detail::readConstant(src, "k" + prefix + "GlyphWidth");
    const std::optional<int> height = detail::readConstant(src, "k" + prefix + "GlyphHeight");
    const std::optional<int> spacingX = detail::readConstant(src, "k" + prefix + "GlyphSpacingX");
    const std::optional<int> spacingY = detail::readConstant(src, "k" + prefix + "GlyphSpacingY");
    if (!width || !height || !spacingX || !spacingY) {
        return "missing k" + prefix + "GlyphWidth / Height / SpacingX / SpacingY";
    }
    if (*width < 1 || *width > kMaxGlyphWidth || *height < 1) {
        return "unsupported glyph cell " + std::to_string(*width) + "x" + std::to_string(*height);
    }
    out.width_ = *width;
    out.height_ = *height;
    out.spacingX_ = *spacingX;
    out.spacingY_ = *spacingY;

    const std::string literalHead = "k" + prefix + "Glyph_";
    const std::string maker = "make" + prefix + "Glyph(";
    for (std::size_t at = src.find(literalHead); at != std::string_view::npos;
         at = src.find(literalHead, at + 1)) {
        if (at > 0 && detail::isIdentChar(src[at - 1])) {
            continue;
        }
        std::size_t cursor = at + literalHead.size();
        const std::size_t nameStart = cursor;
        while (cursor < src.size() && detail::isIdentChar(src[cursor])) {
            ++cursor;
        }
        GlyphLiteral glyph;
        glyph.name_ = std::string(src.substr(nameStart, cursor - nameStart));
        cursor = detail::skipSpaceAndComments(src, cursor);
        if (cursor >= src.size() || src[cursor] != '=') {
            continue;
        }
        cursor = detail::skipSpaceAndComments(src, cursor + 1);
        if (src.compare(cursor, maker.size(), maker) != 0) {
            continue;
        }
        cursor += maker.size();
        while (true) {
            cursor = detail::skipSpaceAndComments(src, cursor);
            if (cursor >= src.size() || src[cursor] != '"') {
                break;
            }
            const std::size_t close = src.find('"', cursor + 1);
            if (close == std::string_view::npos) {
                return "unterminated row in k" + prefix + "Glyph_" + glyph.name_;
            }
            glyph.rowOffsets_.push_back(cursor + 1);
            glyph.rowLengths_.push_back(close - cursor - 1);
            glyph.rows_.push_back(
                detail::packedRow(src.substr(cursor + 1, close - cursor - 1), out.width_)
            );
            cursor = close + 1;
        }
        if (cursor >= src.size() || src[cursor] != ')') {
            glyph.malformed_ = "rows are not one quoted string per row";
        } else if (static_cast<int>(glyph.rows_.size()) != out.height_) {
            glyph.malformed_ = std::to_string(glyph.rows_.size()) + " rows, expected " +
                               std::to_string(out.height_);
        } else {
            for (std::size_t length : glyph.rowLengths_) {
                if (static_cast<int>(length) != out.width_) {
                    glyph.malformed_ = "a row is " + std::to_string(length) + " wide, expected " +
                                       std::to_string(out.width_);
                    break;
                }
            }
        }
        if (findGlyph(out, glyph.name_) >= 0) {
            return "k" + prefix + "Glyph_" + glyph.name_ + " is defined twice";
        }
        out.glyphs_.push_back(std::move(glyph));
    }
    if (out.glyphs_.empty()) {
        return "no " + maker + "...) literal";
    }

    const std::string getter = "get" + prefix + "Glyph(char";
    const std::size_t switchAt = src.find(getter);
    if (switchAt == std::string_view::npos) {
        return "no get" + prefix + "Glyph(char) switch";
    }
    const std::size_t bodyEnd = src.find("\n}", switchAt);
    const std::string_view body = src.substr(switchAt, bodyEnd - switchAt);
    const std::string target = "return &" + literalHead;
    std::vector<char> pending;
    std::size_t cursor = 0;
    while (cursor < body.size()) {
        cursor = detail::skipSpaceAndComments(body, cursor);
        if (body.compare(cursor, 5, "case ") == 0) {
            cursor = detail::skipSpaceAndComments(body, cursor + 5);
            const std::optional<char> c = detail::readCharLiteral(body, cursor);
            cursor = detail::skipSpaceAndComments(body, cursor);
            if (!c || cursor >= body.size() || body[cursor] != ':') {
                return "unreadable case label in get" + prefix + "Glyph";
            }
            pending.push_back(*c);
            ++cursor;
        } else if (body.compare(cursor, 8, "default:") == 0) {
            pending.push_back('\x7f');
            cursor += 8;
        } else if (body.compare(cursor, target.size(), target) == 0) {
            cursor += target.size();
            const std::size_t nameStart = cursor;
            while (cursor < body.size() && detail::isIdentChar(body[cursor])) {
                ++cursor;
            }
            const int glyph = findGlyph(out, body.substr(nameStart, cursor - nameStart));
            for (char c : pending) {
                if (c == '\x7f') {
                    out.fallbackGlyph_ = glyph;
                } else if (static_cast<unsigned char>(c) < 128) {
                    out.charGlyph_[static_cast<unsigned char>(c)] = glyph;
                }
            }
            pending.clear();
        } else {
            ++cursor;
        }
    }
    return {};
}

inline std::string loadFontSource(const std::filesystem::path &path, FontSource &out) {
    std::string text;
    if (!detail::readFile(path, text)) {
        return "cannot read " + path.string();
    }
    out.path_ = path;
    return parseFontSource(std::move(text), out);
}

// The header text with literal `glyph`'s rows replaced by `rows`. A column
// whose bit is unchanged keeps its original character, so only flipped
// texels change bytes.
inline std::string withGlyphRows(
    const FontSource &source, const GlyphLiteral &glyph, const std::vector<std::uint8_t> &rows
) {
    std::string text = source.text_;
    for (std::size_t row = 0; row < glyph.rows_.size(); ++row) {
        if (rows[row] == glyph.rows_[row]) {
            continue;
        }
        for (int col = 0; col < source.width_; ++col) {
            const bool was = IRRender::glyphRowPixel(glyph.rows_[row], source.width_, col);
            const bool now = IRRender::glyphRowPixel(rows[row], source.width_, col);
            if (was != now) {
                text[glyph.rowOffsets_[row] + col] = now ? 'X' : '.';
            }
        }
    }
    return text;
}

// Writes literal `glyphIndex`'s new `rows` back into its header in place and
// reloads `source` from the written text. Refuses, leaving the file
// untouched, when the file no longer matches the loaded snapshot, the literal
// is malformed, or `rows` has the wrong height. Returns an empty string on
// success, else the refusal.
inline std::string
saveGlyph(FontSource &source, int glyphIndex, const std::vector<std::uint8_t> &rows) {
    if (glyphIndex < 0 || glyphIndex >= static_cast<int>(source.glyphs_.size())) {
        return "no literal to save into";
    }
    const GlyphLiteral &glyph = source.glyphs_[glyphIndex];
    if (!glyph.malformed_.empty()) {
        return "k" + source.prefix_ + "Glyph_" + glyph.name_ + " is malformed: " + glyph.malformed_;
    }
    if (static_cast<int>(rows.size()) != source.height_) {
        return "wrong row count";
    }
    std::string onDisk;
    if (!detail::readFile(source.path_, onDisk)) {
        return "cannot read " + source.path_.string();
    }
    if (onDisk != source.text_) {
        return source.path_.filename().string() + " changed on disk since it was loaded; reload it";
    }
    const std::string text = withGlyphRows(source, glyph, rows);

    std::error_code error;
    const std::filesystem::perms perms = std::filesystem::status(source.path_, error).permissions();
    std::filesystem::path temporary = source.path_;
    temporary += ".font-maker-save";
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return "cannot write " + temporary.string();
        }
        file << text;
        if (!file.flush()) {
            file.close();
            std::filesystem::remove(temporary, error);
            return "cannot write " + temporary.string();
        }
    }
    if (perms != std::filesystem::perms::unknown) {
        std::filesystem::permissions(temporary, perms, error);
    }
    std::filesystem::rename(temporary, source.path_, error);
    if (error) {
        // Some Windows runtimes refuse to rename over an existing file; the
        // snapshot check above already proved the old bytes are disposable.
        error.clear();
        std::filesystem::remove(source.path_, error);
        std::filesystem::rename(temporary, source.path_, error);
    }
    if (error) {
        std::filesystem::remove(temporary, error);
        return "cannot replace " + source.path_.string();
    }
    return parseFontSource(text, source);
}

} // namespace FontMaker

#endif /* FONT_MAKER_FONT_SOURCE_H */
