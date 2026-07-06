#ifndef IR_UTILITY_PATH_UTILS_H
#define IR_UTILITY_PATH_UTILS_H

#include <string>

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

} // namespace IRUtility

#endif /* IR_UTILITY_PATH_UTILS_H */
