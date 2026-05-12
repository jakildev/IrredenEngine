#include <irreden/asset/json_sidecar.hpp>
#include <irreden/ir_profile.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace IRAsset {

JsonSidecarWriter::JsonSidecarWriter() {
    m_out.reserve(256);
}

void JsonSidecarWriter::writeIndent() {
    for (std::size_t i = 0; i < m_scopes.size(); ++i) {
        m_out.append("  ");
    }
}

void JsonSidecarWriter::writeEscaped(std::string_view s) {
    m_out.push_back('"');
    for (char c : s) {
        switch (c) {
        case '"':
            m_out.append("\\\"");
            break;
        case '\\':
            m_out.append("\\\\");
            break;
        case '\n':
            m_out.append("\\n");
            break;
        case '\r':
            m_out.append("\\r");
            break;
        case '\t':
            m_out.append("\\t");
            break;
        case '\b':
            m_out.append("\\b");
            break;
        case '\f':
            m_out.append("\\f");
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c) & 0xFF);
                m_out.append(buf);
            } else {
                m_out.push_back(c);
            }
            break;
        }
    }
    m_out.push_back('"');
}

void JsonSidecarWriter::prefixItem() {
    if (m_scopes.empty()) {
        return;
    }
    Scope &s = m_scopes.back();
    if (s.firstItem_) {
        m_out.push_back('\n');
        s.firstItem_ = false;
    } else {
        m_out.append(",\n");
    }
    writeIndent();
}

void JsonSidecarWriter::prefixValue() {
    // A value either follows a key (no scope-level prefix needed; we're on
    // the same line as the key) or appears as a direct array element (needs
    // the comma/newline + indent prefix).
    if (m_keyPending) {
        m_keyPending = false;
        return;
    }
    prefixItem();
}

void JsonSidecarWriter::beginObject() {
    prefixValue();
    m_out.push_back('{');
    m_scopes.push_back({Scope::OBJECT, true});
}

void JsonSidecarWriter::endObject() {
    if (m_scopes.empty() || m_scopes.back().kind_ != Scope::OBJECT) {
        IRE_LOG_ERROR("JsonSidecarWriter::endObject called outside an object scope");
        return;
    }
    const bool empty = m_scopes.back().firstItem_;
    m_scopes.pop_back();
    if (!empty) {
        m_out.push_back('\n');
        writeIndent();
    }
    m_out.push_back('}');
}

void JsonSidecarWriter::beginArray() {
    prefixValue();
    m_out.push_back('[');
    m_scopes.push_back({Scope::ARRAY, true});
}

void JsonSidecarWriter::endArray() {
    if (m_scopes.empty() || m_scopes.back().kind_ != Scope::ARRAY) {
        IRE_LOG_ERROR("JsonSidecarWriter::endArray called outside an array scope");
        return;
    }
    const bool empty = m_scopes.back().firstItem_;
    m_scopes.pop_back();
    if (!empty) {
        m_out.push_back('\n');
        writeIndent();
    }
    m_out.push_back(']');
}

void JsonSidecarWriter::key(std::string_view k) {
    if (m_scopes.empty() || m_scopes.back().kind_ != Scope::OBJECT) {
        IRE_LOG_ERROR("JsonSidecarWriter::key called outside an object scope");
        return;
    }
    prefixItem();
    writeEscaped(k);
    m_out.append(": ");
    m_keyPending = true;
}

void JsonSidecarWriter::valueString(std::string_view s) {
    prefixValue();
    writeEscaped(s);
}

void JsonSidecarWriter::valueInt(std::int64_t v) {
    prefixValue();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    m_out.append(buf);
}

void JsonSidecarWriter::valueUInt(std::uint64_t v) {
    prefixValue();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
    m_out.append(buf);
}

void JsonSidecarWriter::valueFloat(double v) {
    prefixValue();
    // NaN/Inf are not valid JSON. Emit `null` and log; sidecars are
    // human-diffable summaries, not canonical, so this is recoverable.
    if (std::isnan(v) || std::isinf(v)) {
        IRE_LOG_WARN("JsonSidecarWriter: non-finite float emitted as null");
        m_out.append("null");
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%g", v);
    // %g may emit "1" for integer-valued doubles, which still parses as
    // a JSON number — fine.
    m_out.append(buf);
}

void JsonSidecarWriter::valueBool(bool v) {
    prefixValue();
    m_out.append(v ? "true" : "false");
}

void JsonSidecarWriter::valueNull() {
    prefixValue();
    m_out.append("null");
}

bool writeJsonSidecarToFile(const std::string &path, const std::string &json) {
    std::FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) {
        IRE_LOG_ERROR("writeJsonSidecarToFile: fopen failed for {}", path);
        return false;
    }
    const std::size_t written = std::fwrite(json.data(), 1, json.size(), f);
    std::fclose(f);
    if (written != json.size()) {
        IRE_LOG_ERROR(
            "writeJsonSidecarToFile: short write on {} ({} of {} bytes)",
            path,
            written,
            json.size()
        );
        return false;
    }
    return true;
}

} // namespace IRAsset
