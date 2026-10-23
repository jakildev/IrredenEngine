#ifndef IR_UTILITY_PATH_UTILS_H
#define IR_UTILITY_PATH_UTILS_H

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace IRUtility {

std::string
joinPath(const std::string &directory, const std::string &filename, const std::string &extension);

std::string pathWithExtension(const std::string &path, const std::string &extension);

/// Resolve the platform-appropriate per-user data directory for @p appName:
///   - Linux:   `${XDG_DATA_HOME:-$HOME/.local/share}/<appName>`
///   - macOS:   `$HOME/Library/Application Support/<appName>`
///   - Windows: `%APPDATA%\<appName>` (fallback `%USERPROFILE%\<appName>`)
/// Pure path composition — does NOT create the directory (same no-mkdir
/// contract as `joinPath`). Callers that write into it create the directory
/// in their save path via `std::filesystem::create_directories`. If the
/// expected environment variables are absent the bare @p appName is
/// returned as a last-resort relative path.
std::string userDataDir(const std::string &appName);

std::string formatNumberedFilename(
    const std::string &prefix, int index, int width, const std::string &extension
);

/// True iff environment variable @p name is set and non-empty. The single
/// place the engine reads a boolean env toggle (e.g. `IR_PERSIST_DUMP`), so
/// "present but empty" and "unset" both read as false and callers don't each
/// re-derive the getenv-null-and-empty check.
bool envFlagSet(const char *name);

/// The env var's value, or empty when it is unset or empty.
std::string envString(const char *name);

/// The first regular file named @p name plus a suffix from @p suffixes under a
/// `PATH` directory, as `weakly_canonical`. Suffixes are tried in order within
/// each directory before the next directory is searched; nullopt when none
/// matches.
std::optional<std::filesystem::path>
findOnPath(std::string_view name, std::initializer_list<std::string_view> suffixes);

/// Why @p path cannot name a location inside a base directory by its spelling
/// alone, or nullopt when it can. Rejected: empty, a NUL byte, absolute (`/x`,
/// a drive `C:`, root-relative `\x`, UNC `\\host`), and any `..` component.
/// Both separators count on every host, so a path vetted on macOS is vetted the
/// way Windows will read it.
std::optional<std::string_view> containedPathRejection(std::string_view path);

/// Whether @p candidate is @p directory or beneath it, compared component by
/// component, so `<dir>2` is not inside `<dir>`. Lexical: canonicalize both
/// first.
bool isWithinDirectory(
    const std::filesystem::path &directory, const std::filesystem::path &candidate
);

/// @p path joined to @p canonicalBase with every existing component resolved
/// (symlinks, junctions), or nullopt when `containedPathRejection` rejects it
/// or the result is neither @p canonicalBase nor beneath it. Containment is
/// compared component by component, so `<base>2` is not inside `<base>`.
/// @p canonicalBase must already be canonical. This is the one check for every
/// caller-supplied path that must stay under a root.
std::optional<std::filesystem::path>
resolveContainedPath(const std::filesystem::path &canonicalBase, std::string_view path);

/// The id of the base checkout among @ref discoverWorktreeRoots' ids.
inline constexpr std::string_view kBaseWorktreeRoot = ".";

/// Whether @p id is spelled as a worktree root id: `.`, or
/// `.claude/worktrees/<name>` with exactly one non-empty `<name>` component
/// that is not `.` or `..`. Spelling only; nothing on disk is read.
bool isWorktreeRootId(std::string_view id);

/// @p canonicalBase for `.`, or `<base>/.claude/worktrees/<name>` when that is a
/// live worktree: no symlink on the way down, a regular-file `.git` marker (not
/// a symlink), and a real `creations/` directory. nullopt for anything else,
/// including an id @ref isWorktreeRootId rejects. Git is never invoked.
std::optional<std::filesystem::path>
resolveWorktreeRoot(const std::filesystem::path &canonicalBase, std::string_view id);

/// `.` followed by every immediate `.claude/worktrees/<name>` entry that
/// @ref resolveWorktreeRoot accepts, sorted by name.
std::vector<std::string> discoverWorktreeRoots(const std::filesystem::path &canonicalBase);

} // namespace IRUtility

#endif /* IR_UTILITY_PATH_UTILS_H */
