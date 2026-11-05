#ifndef IR_COMMAND_KEYMAP_STORE_H
#define IR_COMMAND_KEYMAP_STORE_H

#include <irreden/command/action_registry.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace IRCommand {

/// The keymap text format: one header line, then one statement per line.
///
/// ```
/// irreden-keymap 1
/// creation voxel_editor
/// bind editor.tool.paint ctrl+P
/// bind editor.tool.paint B
/// unbind editor.view.reset
/// ```
///
/// `bind` lines list an action's effective chords in order (`chordToken`
/// spelling); `unbind` records a deliberately empty action. An action with no
/// line uses its defaults. Lines are sorted by id, so a save is deterministic.
inline constexpr int kKeymapFormatVersion = 1;

/// Overrides `IRUtility::userDataDir("irreden")` as the preferences root.
/// Scripted runs set it to a temporary directory so they never read or write
/// a person's real bindings.
inline constexpr const char *kPreferencesDirEnv = "IR_PREFS_DIR";

enum class KeymapLoadStatus {
    LOADED,
    /// No file yet; defaults stand and the first change creates it.
    MISSING,
    /// The header is not a keymap header. Nothing is applied.
    MALFORMED,
    /// Written by a newer format. Nothing is applied.
    VERSION_TOO_NEW,
    READ_ERROR,
};

struct KeymapLoadResult {
    KeymapLoadStatus status_ = KeymapLoadStatus::MISSING;
    /// Statement lines that were ignored (unknown keyword, unparseable chord).
    std::size_t skippedLines_ = 0;
    std::string message_;
};

enum class KeymapSaveStatus {
    SAVED,
    /// No file is attached.
    DETACHED,
    /// The attached file failed to load as a keymap; it is left untouched
    /// rather than overwritten.
    PROTECTED,
    WRITE_ERROR,
};

struct KeymapSaveResult {
    KeymapSaveStatus status_ = KeymapSaveStatus::DETACHED;
    std::string message_;

    bool ok() const {
        return status_ == KeymapSaveStatus::SAVED;
    }
};

std::string serializeKeymap(std::string_view creation, const ActionOverrides &overrides);

/// Parses @p text into @p overrides. On `MALFORMED` / `VERSION_TOO_NEW`
/// @p overrides is left empty.
KeymapLoadResult parseKeymap(std::string_view text, ActionOverrides &overrides);

/// Writes @p text to `<path>.tmp` and renames it over @p path, creating the
/// parent directory first. On any failure the previous file is untouched and
/// the temporary is removed.
KeymapSaveResult writeFileAtomic(const std::filesystem::path &path, std::string_view text);

/// `$IR_PREFS_DIR` when set, else `IRUtility::userDataDir("irreden")`.
std::filesystem::path preferencesRoot();

/// `<preferencesRoot>/keymaps/<creation>.keymap`.
std::filesystem::path keymapPathFor(std::string_view creation);

/// Ties an `ActionRegistry` to one per-creation keymap file. Owned by
/// `CommandManager` beside the registry; inert until `attach`.
class KeymapStore {
  public:
    explicit KeymapStore(ActionRegistry &actions);
    ~KeymapStore();

    KeymapStore(const KeymapStore &) = delete;
    KeymapStore &operator=(const KeymapStore &) = delete;

    /// Loads @p path into the registry's overrides and, unless it failed to
    /// parse, saves every later binding change back to it. A failed load
    /// leaves the registry's bindings exactly as they were.
    KeymapLoadResult attach(std::filesystem::path path, std::string creation);
    void detach();

    bool attached() const {
        return !m_path.empty();
    }

    const std::filesystem::path &path() const {
        return m_path;
    }

    /// Writes the registry's overrides now. A failed write keeps the in-memory
    /// bindings and the previous file.
    KeymapSaveResult save();

    const KeymapSaveResult &lastSave() const {
        return m_lastSave;
    }

  private:
    ActionRegistry &m_actions;
    std::filesystem::path m_path;
    std::string m_creation;
    bool m_protected = false;
    KeymapSaveResult m_lastSave;
};

} // namespace IRCommand

#endif /* IR_COMMAND_KEYMAP_STORE_H */
