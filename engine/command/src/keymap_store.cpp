#include <irreden/ir_profile.hpp>
#include <irreden/command/keymap_store.hpp>
#include <irreden/utility/path_utils.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace IRCommand {

namespace {

constexpr std::string_view kHeaderKeyword = "irreden-keymap";

std::vector<std::string_view> splitWords(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            ++i;
        }
        if (i > start) {
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

} // namespace

std::string serializeKeymap(std::string_view creation, const ActionOverrides &overrides) {
    std::string text =
        std::string(kHeaderKeyword) + " " + std::to_string(kKeymapFormatVersion) + "\n";
    if (!creation.empty()) {
        text += "creation " + std::string(creation) + "\n";
    }
    for (const auto &[id, chords] : overrides) {
        if (chords.empty()) {
            text += "unbind " + id + "\n";
            continue;
        }
        for (const KeyChord &chord : chords) {
            text += "bind " + id + " " + chordToken(chord) + "\n";
        }
    }
    return text;
}

KeymapLoadResult parseKeymap(std::string_view text, ActionOverrides &overrides) {
    overrides.clear();
    KeymapLoadResult result{KeymapLoadStatus::LOADED, 0, ""};
    bool sawHeader = false;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++lineNumber;
        const std::vector<std::string_view> words = splitWords(line);
        if (words.empty() || words[0].starts_with('#')) {
            continue;
        }
        if (!sawHeader) {
            int version = 0;
            if (words.size() != 2 || words[0] != kHeaderKeyword ||
                std::sscanf(std::string(words[1]).c_str(), "%d", &version) != 1 || version < 1) {
                overrides.clear();
                return {KeymapLoadStatus::MALFORMED, 0, "line 1 is not a keymap header"};
            }
            if (version > kKeymapFormatVersion) {
                return {
                    KeymapLoadStatus::VERSION_TOO_NEW,
                    0,
                    "keymap version " + std::to_string(version) + " is newer than " +
                        std::to_string(kKeymapFormatVersion)
                };
            }
            sawHeader = true;
            continue;
        }
        if (words[0] == "creation" && words.size() == 2) {
            continue;
        }
        if (words[0] == "unbind" && words.size() == 2) {
            overrides.try_emplace(std::string(words[1]));
            continue;
        }
        if (words[0] == "bind" && words.size() == 3) {
            const std::optional<KeyChord> chord = parseChordToken(words[2]);
            if (chord && isAssignableChord(*chord)) {
                std::vector<KeyChord> &chords = overrides[std::string(words[1])];
                if (std::find(chords.begin(), chords.end(), *chord) == chords.end()) {
                    chords.push_back(*chord);
                }
                continue;
            }
        }
        ++result.skippedLines_;
        IRE_LOG_WARN("Keymap line {} ignored: '{}'", lineNumber, std::string(line));
    }
    if (!sawHeader) {
        return {KeymapLoadStatus::MALFORMED, 0, "keymap is empty"};
    }
    return result;
}

KeymapSaveResult writeFileAtomic(const std::filesystem::path &path, std::string_view text) {
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return {KeymapSaveStatus::WRITE_ERROR, "cannot create " + path.parent_path().string()};
        }
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, error);
            return {KeymapSaveStatus::WRITE_ERROR, "cannot write " + temporary.string()};
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {KeymapSaveStatus::WRITE_ERROR, "cannot replace " + path.string()};
    }
    return {KeymapSaveStatus::SAVED, ""};
}

std::filesystem::path preferencesRoot() {
    const std::string overrideDir = IRUtility::envString(kPreferencesDirEnv);
    if (!overrideDir.empty()) {
        return overrideDir;
    }
    return IRUtility::userDataDir("irreden");
}

std::filesystem::path keymapPathFor(std::string_view creation) {
    return preferencesRoot() / "keymaps" / (std::string(creation) + ".keymap");
}

KeymapStore::KeymapStore(ActionRegistry &actions)
    : m_actions(actions) {}

KeymapStore::~KeymapStore() {
    m_actions.setOnBindingsChanged(nullptr);
}

KeymapLoadResult KeymapStore::attach(std::filesystem::path path, std::string creation) {
    m_path = std::move(path);
    m_creation = std::move(creation);
    m_protected = false;
    m_actions.setOnBindingsChanged([this]() { save(); });

    std::error_code error;
    if (!std::filesystem::exists(m_path, error)) {
        return {KeymapLoadStatus::MISSING, 0, ""};
    }
    std::ifstream in(m_path, std::ios::binary);
    if (!in) {
        m_protected = true;
        return {KeymapLoadStatus::READ_ERROR, 0, "cannot read " + m_path.string()};
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    ActionOverrides overrides;
    KeymapLoadResult result = parseKeymap(contents.str(), overrides);
    if (result.status_ != KeymapLoadStatus::LOADED) {
        m_protected = true;
        IRE_LOG_WARN(
            "Keymap {} not applied ({}); it is left untouched",
            m_path.string(),
            result.message_
        );
        return result;
    }
    m_actions.setOverrides(std::move(overrides));
    return result;
}

void KeymapStore::detach() {
    m_actions.setOnBindingsChanged(nullptr);
    m_path.clear();
    m_creation.clear();
    m_protected = false;
    m_lastSave = {};
}

KeymapSaveResult KeymapStore::save() {
    if (m_path.empty()) {
        m_lastSave = {KeymapSaveStatus::DETACHED, ""};
    } else if (m_protected) {
        m_lastSave = {KeymapSaveStatus::PROTECTED, m_path.string() + " did not load as a keymap"};
    } else {
        m_lastSave = writeFileAtomic(m_path, serializeKeymap(m_creation, m_actions.overrides()));
        if (!m_lastSave.ok()) {
            IRE_LOG_ERROR("Keymap save failed: {}", m_lastSave.message_);
        }
    }
    return m_lastSave;
}

} // namespace IRCommand
