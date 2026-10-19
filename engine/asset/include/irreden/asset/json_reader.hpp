#ifndef IR_ASSET_JSON_READER_H
#define IR_ASSET_JSON_READER_H

/// Minimal stdlib-only JSON reader for hand-authored manifests
/// (`creation_manifest.hpp`). It has no engine includes, so a standalone tool
/// can compile `json_reader.cpp` directly. Sidecars are not read through it:
/// they stay write-only (`json_sidecar.hpp`).
///
/// `parseJson` builds an immutable `JsonValue` tree. Every value records the
/// 1-based line and column of its first byte, so a schema layer can report a
/// semantic error at the offending value. Columns count bytes, not code points.
///
/// Accepted: RFC 8259 values — objects, arrays, strings (every escape,
/// including `\uXXXX` surrogate pairs, decoded to UTF-8), numbers, `true`,
/// `false`, `null` — plus a leading UTF-8 byte-order mark. Rejected with a
/// `JsonError`: trailing commas, comments, unescaped control characters in
/// strings, lone surrogates, leading zeros, a number outside the double range,
/// content after the root value, and nesting deeper than `kMaxJsonDepth`.
///
/// Numbers are parsed as `double` through `std::from_chars`, never the C
/// locale. Integers beyond 2^53 lose precision. Bytes >= 0x80 in strings pass
/// through unvalidated. Duplicate object keys are kept in source order;
/// `find` returns the last one.
///
/// Recursion depth tracks nesting only; array elements and object members are
/// parsed in a loop, so a flat array of any length uses constant stack.

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace IRAsset {

inline constexpr std::uint32_t kMaxJsonDepth = 512;

enum class JsonType : std::uint8_t {
    NULL_VALUE = 0,
    BOOLEAN,
    NUMBER,
    STRING,
    ARRAY,
    OBJECT,
};

/// Lower-case JSON name of @p type (`"null"`, `"boolean"`, ...), for
/// diagnostics.
std::string_view jsonTypeName(JsonType type);

struct JsonError {
    std::uint32_t line_ = 0;
    std::uint32_t column_ = 0;
    std::string message_;
};

class JsonValue;
struct JsonMember;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::vector<JsonMember>;

namespace detail {
class JsonParser;
} // namespace detail

class JsonValue {
  public:
    /// A null value at line 0, column 0 — only the parser builds positioned
    /// values.
    JsonValue() = default;

    JsonType type() const {
        return static_cast<JsonType>(m_value.index());
    }
    bool isNull() const {
        return type() == JsonType::NULL_VALUE;
    }
    bool isBool() const {
        return type() == JsonType::BOOLEAN;
    }
    bool isNumber() const {
        return type() == JsonType::NUMBER;
    }
    bool isString() const {
        return type() == JsonType::STRING;
    }
    bool isArray() const {
        return type() == JsonType::ARRAY;
    }
    bool isObject() const {
        return type() == JsonType::OBJECT;
    }

    /// Each accessor returns empty / null when the value is another type.
    std::optional<bool> asBool() const;
    std::optional<double> asNumber() const;
    /// The number when it is integral and fits `int64_t`; empty otherwise.
    std::optional<std::int64_t> asInt() const;
    const std::string *asString() const;
    const JsonArray *asArray() const;
    const JsonObject *asObject() const;

    /// The last member named @p key, or null when absent or not an object.
    const JsonValue *find(std::string_view key) const;

    std::uint32_t line() const {
        return m_line;
    }
    std::uint32_t column() const {
        return m_column;
    }

  private:
    friend class detail::JsonParser;

    // Alternative order matches `JsonType`.
    std::variant<std::monostate, bool, double, std::string, JsonArray, JsonObject> m_value;
    std::uint32_t m_line = 0;
    std::uint32_t m_column = 0;
};

struct JsonMember {
    std::string key_;
    JsonValue value_;
};

std::expected<JsonValue, JsonError> parseJson(std::string_view text);

} // namespace IRAsset

#endif /* IR_ASSET_JSON_READER_H */
