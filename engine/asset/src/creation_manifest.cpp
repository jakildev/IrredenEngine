#include <irreden/asset/creation_manifest.hpp>

#include <irreden/asset/json_reader.hpp>

#include <array>
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
        out.display_ = out.name_;
        if (!readOptionalString(value, "display", path, out.display_) ||
            !readOptionalString(value, "description", path, out.description_)) {
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
               readOptionalString(root, "thumbnail", "", out.thumbnail_) &&
               readStringArray(root, "docs", "", out.docs_) && readTargets(root, out.targets_);
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

} // namespace IRAsset
