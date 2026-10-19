#include <irreden/asset/json_reader.hpp>

#include <charconv>
#include <cstdio>
#include <system_error>
#include <utility>

namespace IRAsset {

std::string_view jsonTypeName(JsonType type) {
    switch (type) {
    case JsonType::NULL_VALUE:
        return "null";
    case JsonType::BOOLEAN:
        return "boolean";
    case JsonType::NUMBER:
        return "number";
    case JsonType::STRING:
        return "string";
    case JsonType::ARRAY:
        return "array";
    case JsonType::OBJECT:
        return "object";
    }
    return "unknown";
}

std::optional<bool> JsonValue::asBool() const {
    if (const bool *value = std::get_if<bool>(&m_value)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<double> JsonValue::asNumber() const {
    if (const double *value = std::get_if<double>(&m_value)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<std::int64_t> JsonValue::asInt() const {
    const double *value = std::get_if<double>(&m_value);
    // 2^63 is exactly representable; every double in [-2^63, 2^63) converts
    // without overflow, and the round trip rejects fractions.
    constexpr double kTwoTo63 = 9223372036854775808.0;
    if (value == nullptr || !(*value >= -kTwoTo63 && *value < kTwoTo63)) {
        return std::nullopt;
    }
    const auto truncated = static_cast<std::int64_t>(*value);
    if (static_cast<double>(truncated) != *value) {
        return std::nullopt;
    }
    return truncated;
}

const std::string *JsonValue::asString() const {
    return std::get_if<std::string>(&m_value);
}

const JsonArray *JsonValue::asArray() const {
    return std::get_if<JsonArray>(&m_value);
}

const JsonObject *JsonValue::asObject() const {
    return std::get_if<JsonObject>(&m_value);
}

const JsonValue *JsonValue::find(std::string_view key) const {
    const JsonObject *members = asObject();
    if (members == nullptr) {
        return nullptr;
    }
    for (auto it = members->rbegin(); it != members->rend(); ++it) {
        if (it->key_ == key) {
            return &it->value_;
        }
    }
    return nullptr;
}

namespace detail {

class JsonParser {
  public:
    explicit JsonParser(std::string_view text)
        : m_text(text) {}

    std::expected<JsonValue, JsonError> parseDocument() {
        if (m_text.starts_with("\xEF\xBB\xBF")) {
            m_pos = 3;
            m_lineStart = 3;
        }
        skipWhitespace();
        JsonValue root;
        if (!parseValue(root, 0)) {
            return std::unexpected(std::move(m_error));
        }
        skipWhitespace();
        if (m_pos != m_text.size()) {
            fail(m_pos, "unexpected content after the root value");
            return std::unexpected(std::move(m_error));
        }
        return root;
    }

  private:
    bool atEnd() const {
        return m_pos >= m_text.size();
    }

    char peek() const {
        return m_text[m_pos];
    }

    // Strings reject raw control characters, so a newline is only ever seen
    // here and every offset handed to `fail` lies on the current line.
    void skipWhitespace() {
        while (!atEnd()) {
            const char c = peek();
            if (c == '\n') {
                ++m_line;
                m_lineStart = m_pos + 1;
            } else if (c != ' ' && c != '\t' && c != '\r') {
                return;
            }
            ++m_pos;
        }
    }

    std::uint32_t columnOf(std::size_t offset) const {
        return static_cast<std::uint32_t>(offset - m_lineStart + 1);
    }

    bool fail(std::size_t offset, std::string message) {
        m_error.line_ = m_line;
        m_error.column_ = columnOf(offset);
        m_error.message_ = std::move(message);
        return false;
    }

    bool failUnexpected(std::size_t offset) {
        if (offset >= m_text.size()) {
            return fail(offset, "unexpected end of input");
        }
        const auto byte = static_cast<unsigned char>(m_text[offset]);
        char description[48];
        if (byte >= 0x20 && byte < 0x7F) {
            std::snprintf(description, sizeof(description), "unexpected character '%c'", byte);
        } else {
            std::snprintf(description, sizeof(description), "unexpected byte 0x%02X", byte);
        }
        return fail(offset, description);
    }

    void stamp(JsonValue &value) const {
        value.m_line = m_line;
        value.m_column = columnOf(m_pos);
    }

    // Precondition: whitespace before the value has been skipped.
    bool parseValue(JsonValue &out, std::uint32_t depth) {
        if (atEnd()) {
            return failUnexpected(m_pos);
        }
        stamp(out);
        switch (peek()) {
        case '{':
            return parseObject(out, depth);
        case '[':
            return parseArray(out, depth);
        case '"': {
            std::string text;
            if (!parseString(text)) {
                return false;
            }
            out.m_value = std::move(text);
            return true;
        }
        case 't':
            return parseLiteral(out, "true", true);
        case 'f':
            return parseLiteral(out, "false", false);
        case 'n':
            return parseLiteral(out, "null", std::monostate{});
        default:
            return parseNumber(out);
        }
    }

    template <typename T> bool parseLiteral(JsonValue &out, std::string_view word, T value) {
        if (m_text.substr(m_pos, word.size()) != word) {
            return fail(m_pos, "invalid literal");
        }
        m_pos += word.size();
        out.m_value = value;
        return true;
    }

    bool enterContainer(std::uint32_t depth) {
        if (depth >= kMaxJsonDepth) {
            return fail(m_pos, "nesting deeper than " + std::to_string(kMaxJsonDepth));
        }
        ++m_pos;
        skipWhitespace();
        return true;
    }

    bool parseArray(JsonValue &out, std::uint32_t depth) {
        if (!enterContainer(depth)) {
            return false;
        }
        JsonArray elements;
        if (!atEnd() && peek() == ']') {
            ++m_pos;
            out.m_value = std::move(elements);
            return true;
        }
        while (true) {
            JsonValue element;
            if (!parseValue(element, depth + 1)) {
                return false;
            }
            elements.push_back(std::move(element));
            skipWhitespace();
            if (atEnd()) {
                return failUnexpected(m_pos);
            }
            if (peek() == ']') {
                ++m_pos;
                break;
            }
            if (peek() != ',') {
                return fail(m_pos, "expected ',' or ']' in array");
            }
            ++m_pos;
            skipWhitespace();
            if (!atEnd() && peek() == ']') {
                return fail(m_pos, "trailing comma in array");
            }
        }
        out.m_value = std::move(elements);
        return true;
    }

    bool parseObject(JsonValue &out, std::uint32_t depth) {
        if (!enterContainer(depth)) {
            return false;
        }
        JsonObject members;
        if (!atEnd() && peek() == '}') {
            ++m_pos;
            out.m_value = std::move(members);
            return true;
        }
        while (true) {
            if (atEnd() || peek() != '"') {
                return atEnd() ? failUnexpected(m_pos) : fail(m_pos, "expected a string key");
            }
            JsonMember member;
            if (!parseString(member.key_)) {
                return false;
            }
            skipWhitespace();
            if (atEnd() || peek() != ':') {
                return atEnd() ? failUnexpected(m_pos) : fail(m_pos, "expected ':' after key");
            }
            ++m_pos;
            skipWhitespace();
            if (!parseValue(member.value_, depth + 1)) {
                return false;
            }
            members.push_back(std::move(member));
            skipWhitespace();
            if (atEnd()) {
                return failUnexpected(m_pos);
            }
            if (peek() == '}') {
                ++m_pos;
                break;
            }
            if (peek() != ',') {
                return fail(m_pos, "expected ',' or '}' in object");
            }
            ++m_pos;
            skipWhitespace();
            if (!atEnd() && peek() == '}') {
                return fail(m_pos, "trailing comma in object");
            }
        }
        out.m_value = std::move(members);
        return true;
    }

    static int hexDigit(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    // Reads the four hex digits after the `\u` at @p escapeOffset.
    bool readHex4(std::size_t escapeOffset, std::uint32_t &out) {
        if (m_text.size() - escapeOffset < 6) {
            return fail(escapeOffset, "truncated \\u escape");
        }
        out = 0;
        for (std::size_t i = escapeOffset + 2; i < escapeOffset + 6; ++i) {
            const int digit = hexDigit(m_text[i]);
            if (digit < 0) {
                return fail(escapeOffset, "invalid hex digit in \\u escape");
            }
            out = (out << 4) | static_cast<std::uint32_t>(digit);
        }
        return true;
    }

    static void appendUtf8(std::string &out, std::uint32_t codePoint) {
        if (codePoint < 0x80) {
            out.push_back(static_cast<char>(codePoint));
        } else if (codePoint < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    // Leaves `m_pos` after the `\uXXXX` (or the pair) at `m_pos`.
    bool parseUnicodeEscape(std::string &out) {
        const std::size_t escapeOffset = m_pos;
        std::uint32_t unit = 0;
        if (!readHex4(escapeOffset, unit)) {
            return false;
        }
        m_pos += 6;
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return fail(escapeOffset, "lone low surrogate in \\u escape");
        }
        if (unit < 0xD800 || unit > 0xDBFF) {
            appendUtf8(out, unit);
            return true;
        }
        std::uint32_t low = 0;
        if (m_text.substr(m_pos, 2) != "\\u" || !readHex4(m_pos, low) || low < 0xDC00 ||
            low > 0xDFFF) {
            return fail(escapeOffset, "lone high surrogate in \\u escape");
        }
        m_pos += 6;
        appendUtf8(out, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
        return true;
    }

    // Precondition: `peek() == '"'`.
    bool parseString(std::string &out) {
        const std::size_t openOffset = m_pos;
        ++m_pos;
        while (true) {
            const std::size_t runStart = m_pos;
            while (!atEnd()) {
                const auto c = static_cast<unsigned char>(peek());
                if (c == '"' || c == '\\' || c < 0x20) {
                    break;
                }
                ++m_pos;
            }
            out.append(m_text.substr(runStart, m_pos - runStart));
            if (atEnd()) {
                return fail(openOffset, "unterminated string");
            }
            const char c = peek();
            if (c == '"') {
                ++m_pos;
                return true;
            }
            if (c != '\\') {
                return fail(m_pos, "unescaped control character in string");
            }
            if (m_pos + 1 >= m_text.size()) {
                return fail(openOffset, "unterminated string");
            }
            const char escaped = m_text[m_pos + 1];
            if (escaped == 'u') {
                if (!parseUnicodeEscape(out)) {
                    return false;
                }
                continue;
            }
            char decoded = 0;
            switch (escaped) {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            default:
                return fail(m_pos, "invalid escape sequence");
            }
            out.push_back(decoded);
            m_pos += 2;
        }
    }

    bool isDigitAt(std::size_t offset) const {
        return offset < m_text.size() && m_text[offset] >= '0' && m_text[offset] <= '9';
    }

    void skipDigits() {
        while (isDigitAt(m_pos)) {
            ++m_pos;
        }
    }

    // Validates the RFC 8259 number grammar before `from_chars`, which would
    // otherwise accept forms JSON forbids (`inf`, `nan`, `.5`).
    bool parseNumber(JsonValue &out) {
        const std::size_t start = m_pos;
        if (peek() == '-') {
            ++m_pos;
        }
        if (!isDigitAt(m_pos)) {
            return start == m_pos ? failUnexpected(m_pos) : fail(start, "invalid number");
        }
        if (m_text[m_pos] == '0') {
            ++m_pos;
            if (isDigitAt(m_pos)) {
                return fail(start, "invalid number: leading zero");
            }
        } else {
            skipDigits();
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            if (!isDigitAt(m_pos)) {
                return fail(start, "invalid number: no digits after '.'");
            }
            skipDigits();
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) {
                ++m_pos;
            }
            if (!isDigitAt(m_pos)) {
                return fail(start, "invalid number: no digits in exponent");
            }
            skipDigits();
        }
        double value = 0.0;
        const char *first = m_text.data() + start;
        const char *last = m_text.data() + m_pos;
        const auto [end, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || end != last) {
            return fail(start, "number out of range");
        }
        out.m_value = value;
        return true;
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    std::size_t m_lineStart = 0;
    std::uint32_t m_line = 1;
    JsonError m_error;
};

} // namespace detail

std::expected<JsonValue, JsonError> parseJson(std::string_view text) {
    return detail::JsonParser(text).parseDocument();
}

} // namespace IRAsset
