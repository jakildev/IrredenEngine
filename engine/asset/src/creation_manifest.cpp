#include <irreden/asset/creation_manifest.hpp>

#include <irreden/asset/json_reader.hpp>
#include <irreden/utility/path_utils.hpp>

#include <array>
#include <cstdio>
#include <optional>
#include <utility>

namespace IRAsset {

namespace {

struct KindName {
    std::string_view name_;
    CreationKind kind_;
};

constexpr std::array<KindName, 5> kKindNames{{
    {"demo", CreationKind::DEMO},
    {"editor", CreationKind::EDITOR},
    {"game", CreationKind::GAME},
    {"tool", CreationKind::TOOL},
    {"test", CreationKind::TEST},
}};

std::string memberPath(const std::string &parent, std::string_view key) {
    if (parent.empty()) {
        return std::string(key);
    }
    std::string path = parent;
    path.push_back('.');
    path.append(key);
    return path;
}

std::string indexPath(const std::string &parent, std::size_t index) {
    return parent + "[" + std::to_string(index) + "]";
}

bool isAsciiAlnum(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

bool isTargetNameStart(char c) {
    return isAsciiAlnum(c) || c == '_';
}

bool isTargetNameChar(char c) {
    return isTargetNameStart(c) || c == '.' || c == '+' || c == '-';
}

bool equalsIgnoringAsciiCase(std::string_view text, std::string_view upper) {
    if (text.size() != upper.size()) {
        return false;
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if ((c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c) != upper[i]) {
            return false;
        }
    }
    return true;
}

bool isReservedDeviceStem(std::string_view stem) {
    for (std::string_view device : {"CON", "PRN", "AUX", "NUL"}) {
        if (equalsIgnoringAsciiCase(stem, device)) {
            return true;
        }
    }
    if (stem.size() != 4 || stem[3] < '0' || stem[3] > '9') {
        return false;
    }
    const std::string_view prefix = stem.substr(0, 3);
    return equalsIgnoringAsciiCase(prefix, "COM") || equalsIgnoringAsciiCase(prefix, "LPT");
}

std::string quotedChar(char c) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte >= 0x7f) {
        char hex[8];
        std::snprintf(hex, sizeof(hex), "0x%02x", byte);
        return std::string("byte ") + hex;
    }
    return std::string("'") + c + "'";
}

// Empty when @p name passes `isTargetName`, else why it fails.
std::string targetNameRejection(std::string_view name) {
    if (name.empty()) {
        return "must not be empty";
    }
    if (!isTargetNameStart(name.front())) {
        return "leading " + quotedChar(name.front());
    }
    for (const char c : name) {
        if (!isTargetNameChar(c)) {
            return quotedChar(c) + " is not allowed in a target name";
        }
    }
    const std::string_view stem = name.substr(0, name.find('.'));
    if (isReservedDeviceStem(stem)) {
        return "reserved Windows device stem '" + std::string(stem) + "'";
    }
    return {};
}

class ManifestParser {
  public:
    std::expected<CreationManifest, ManifestError> parse(const JsonValue &root) {
        CreationManifest manifest;
        if (!readManifest(root, manifest)) {
            return std::unexpected(std::move(m_error));
        }
        return manifest;
    }

  private:
    bool fail(const JsonValue &at, std::string path, std::string message) {
        m_error.line_ = at.line();
        m_error.column_ = at.column();
        m_error.path_ = std::move(path);
        m_error.message_ = std::move(message);
        return false;
    }

    bool failType(const JsonValue &at, std::string path, std::string_view expected) {
        std::string message = "expected ";
        message.append(expected);
        message.append(", got ");
        message.append(jsonTypeName(at.type()));
        return fail(at, std::move(path), std::move(message));
    }

    bool failed() const {
        return !m_error.message_.empty();
    }

    // Null when @p key is absent (the caller keeps its default) or has the
    // wrong type (an error is recorded; check `failed()`).
    const JsonValue *findTyped(
        const JsonValue &object,
        std::string_view key,
        const std::string &parent,
        JsonType type,
        std::string_view expected
    ) {
        const JsonValue *value = object.find(key);
        if (value != nullptr && value->type() != type) {
            failType(*value, memberPath(parent, key), expected);
            return nullptr;
        }
        return value;
    }

    bool readOptionalString(
        const JsonValue &object, std::string_view key, const std::string &parent, std::string &out
    ) {
        if (const JsonValue *value = findTyped(object, key, parent, JsonType::STRING, "a string")) {
            out = *value->asString();
        }
        return !failed();
    }

    bool readRequiredString(
        const JsonValue &object, std::string_view key, const std::string &parent, std::string &out
    ) {
        const JsonValue *value = object.find(key);
        if (value == nullptr) {
            return fail(object, memberPath(parent, key), "missing required key");
        }
        if (!value->isString()) {
            return failType(*value, memberPath(parent, key), "a string");
        }
        if (value->asString()->empty()) {
            return fail(*value, memberPath(parent, key), "must not be empty");
        }
        out = *value->asString();
        return true;
    }

    bool readOptionalBool(
        const JsonValue &object, std::string_view key, const std::string &parent, bool &out
    ) {
        if (const JsonValue *value =
                findTyped(object, key, parent, JsonType::BOOLEAN, "a boolean")) {
            out = *value->asBool();
        }
        return !failed();
    }

    bool readStringArray(
        const JsonValue &object,
        std::string_view key,
        const std::string &parent,
        std::vector<std::string> &out
    ) {
        const JsonValue *value = findTyped(object, key, parent, JsonType::ARRAY, "an array");
        if (value == nullptr) {
            return !failed();
        }
        const JsonArray &elements = *value->asArray();
        const std::string path = memberPath(parent, key);
        out.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i) {
            const std::string *text = elements[i].asString();
            if (text == nullptr) {
                return failType(elements[i], indexPath(path, i), "a string");
            }
            out.push_back(*text);
        }
        return true;
    }

    bool checkContainedPath(const JsonValue &at, std::string path, std::string_view text) {
        if (const std::optional<std::string_view> reason =
                IRUtility::containedPathRejection(text)) {
            return fail(at, std::move(path), std::string(*reason));
        }
        return true;
    }

    bool readOptionalPath(const JsonValue &object, std::string_view key, std::string &out) {
        const JsonValue *value = findTyped(object, key, "", JsonType::STRING, "a string");
        if (value == nullptr) {
            return !failed();
        }
        out = *value->asString();
        return checkContainedPath(*value, std::string(key), out);
    }

    bool
    readPathArray(const JsonValue &object, std::string_view key, std::vector<std::string> &out) {
        if (!readStringArray(object, key, "", out) || out.empty()) {
            return !failed();
        }
        const JsonArray &elements = *object.find(key)->asArray();
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (!checkContainedPath(elements[i], indexPath(std::string(key), i), out[i])) {
                return false;
            }
        }
        return true;
    }

    bool readVersion(const JsonValue &root, std::int64_t &out) {
        const JsonValue *value = root.find("version");
        if (value == nullptr) {
            return fail(root, "version", "missing required key");
        }
        if (!value->isNumber()) {
            return failType(*value, "version", "an integer");
        }
        const std::optional<std::int64_t> version = value->asInt();
        if (version != kCreationManifestVersion) {
            return fail(
                *value,
                "version",
                "unsupported manifest version (this reader understands " +
                    std::to_string(kCreationManifestVersion) + ")"
            );
        }
        out = *version;
        return true;
    }

    bool readKind(const JsonValue &root, CreationKind &out) {
        const JsonValue *value = findTyped(root, "kind", "", JsonType::STRING, "a string");
        if (value == nullptr) {
            return !failed();
        }
        const std::string &text = *value->asString();
        for (const KindName &entry : kKindNames) {
            if (entry.name_ == text) {
                out = entry.kind_;
                return true;
            }
        }
        return fail(
            *value,
            "kind",
            "unknown kind '" + text + "' (expected demo, editor, game, tool or test)"
        );
    }

    bool readPreset(const JsonValue &value, const std::string &path, CreationPreset &out) {
        if (!value.isObject()) {
            return failType(value, path, "an object");
        }
        return readRequiredString(value, "name", path, out.name_) &&
               readStringArray(value, "args", path, out.args_);
    }

    bool readTarget(const JsonValue &value, const std::string &path, CreationTarget &out) {
        if (!value.isObject()) {
            return failType(value, path, "an object");
        }
        if (!readRequiredString(value, "name", path, out.name_)) {
            return false;
        }
        if (std::string reason = targetNameRejection(out.name_); !reason.empty()) {
            return fail(*value.find("name"), memberPath(path, "name"), std::move(reason));
        }
        out.display_ = out.name_;
        if (!readOptionalString(value, "display", path, out.display_) ||
            !readOptionalString(value, "description", path, out.description_) ||
            !readOptionalBool(value, "package", path, out.package_)) {
            return false;
        }
        const JsonValue *presets = findTyped(value, "presets", path, JsonType::ARRAY, "an array");
        if (presets == nullptr) {
            return !failed();
        }
        const JsonArray &elements = *presets->asArray();
        const std::string presetsPath = memberPath(path, "presets");
        out.presets_.resize(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i) {
            if (!readPreset(elements[i], indexPath(presetsPath, i), out.presets_[i])) {
                return false;
            }
        }
        return true;
    }

    bool readTargets(const JsonValue &root, std::vector<CreationTarget> &out) {
        const JsonValue *value = root.find("targets");
        if (value == nullptr) {
            return fail(root, "targets", "missing required key");
        }
        const JsonArray *targets = value->asArray();
        if (targets == nullptr) {
            return failType(*value, "targets", "an array");
        }
        out.resize(targets->size());
        for (std::size_t i = 0; i < targets->size(); ++i) {
            if (!readTarget((*targets)[i], indexPath("targets", i), out[i])) {
                return false;
            }
        }
        return true;
    }

    bool readManifest(const JsonValue &root, CreationManifest &out) {
        if (!root.isObject()) {
            return failType(root, "", "an object at the manifest root");
        }
        return readVersion(root, out.version_) && readRequiredString(root, "name", "", out.name_) &&
               readOptionalString(root, "description", "", out.description_) &&
               readOptionalString(root, "group", "", out.group_) &&
               readStringArray(root, "tags", "", out.tags_) && readKind(root, out.kind_) &&
               readOptionalPath(root, "thumbnail", out.thumbnail_) &&
               readPathArray(root, "docs", out.docs_) && readTargets(root, out.targets_);
    }

    ManifestError m_error;
};

} // namespace

std::expected<CreationManifest, ManifestError> parseCreationManifest(std::string_view text) {
    std::expected<JsonValue, JsonError> root = parseJson(text);
    if (!root) {
        return std::unexpected(
            ManifestError{root.error().line_, root.error().column_, "", root.error().message_}
        );
    }
    return ManifestParser().parse(*root);
}

bool isTargetName(std::string_view name) {
    return targetNameRejection(name).empty();
}

} // namespace IRAsset
