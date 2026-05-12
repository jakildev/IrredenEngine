#ifndef IR_ASSET_JSON_SIDECAR_H
#define IR_ASSET_JSON_SIDECAR_H

/// Minimal stdlib-only JSON emitter for asset sidecars. No third-party
/// JSON dependency — sidecars are write-only and small, and the binary
/// asset is always the source of truth.
///
/// **Read side is not implemented by design.** Save Format Extensibility
/// Rule #6: the JSON sidecar is regenerated from the binary on every
/// save and ignored on load. Extending the binary side never forces a
/// sidecar schema migration; the emitter just learns the new field.
///
/// Supported shape: any nesting of objects + arrays containing strings,
/// integers, floats, and booleans. The emitter pretty-prints with
/// 2-space indentation.
///
/// Caller usage:
///
///     JsonSidecarWriter j;
///     j.beginObject();
///         j.key("version"); j.valueInt(1);
///         j.key("name");    j.valueString("torus_knot");
///         j.key("voxels");
///         j.beginArray();
///             j.beginObject();
///                 j.key("xyz"); j.beginArray();
///                     j.valueInt(0); j.valueInt(1); j.valueInt(2);
///                 j.endArray();
///                 j.key("color"); j.valueString("#ff8800");
///             j.endObject();
///         j.endArray();
///     j.endObject();
///     std::string out = j.str();

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace IRAsset {

class JsonSidecarWriter {
  public:
    JsonSidecarWriter();

    /// Begin a `{ ... }` object. Inside, alternate `key(...)` then a
    /// `value*` or `begin*`. Caller must close with `endObject()`.
    void beginObject();
    void endObject();

    /// Begin a `[ ... ]` array. Inside, repeated `value*` or `begin*`
    /// without keys. Caller must close with `endArray()`.
    void beginArray();
    void endArray();

    /// Emit a key inside the current object scope. Must be followed by
    /// exactly one value or begin*.
    void key(std::string_view k);

    void valueString(std::string_view s);
    void valueInt(std::int64_t v);
    void valueUInt(std::uint64_t v);
    void valueFloat(double v);
    void valueBool(bool v);
    void valueNull();

    /// Final JSON document as a string. Safe to call mid-build but the
    /// caller almost always wants it after the outer scope closes.
    std::string str() const {
        return m_out;
    }

  private:
    /// Scope tracker — top-of-stack records which container is open and
    /// whether the next item is the first (commas live between items).
    struct Scope {
        enum Kind { OBJECT, ARRAY };
        Kind kind_;
        bool firstItem_ = true;
    };

    void prefixItem();
    void prefixValue();
    void writeIndent();
    void writeEscaped(std::string_view s);

    std::string m_out;
    std::vector<Scope> m_scopes;
    bool m_keyPending = false; // a key has been emitted; next value attaches to it
};

/// Convenience: write the sidecar string to @p path. Returns false on
/// I/O failure.
bool writeJsonSidecarToFile(const std::string &path, const std::string &json);

} // namespace IRAsset

#endif /* IR_ASSET_JSON_SIDECAR_H */
