#ifndef COMPOSITE_SAVE_TRANSACTION_H
#define COMPOSITE_SAVE_TRANSACTION_H

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace IRVoxelEditor {

// Stages a fixed-name document bundle and rolls back failed publication. Files
// publish in registration order, so the manifest must be registered last. This
// is not crash/power-loss atomic or a snapshot boundary for concurrent readers.
class CompositeSaveTransaction {
  public:
    CompositeSaveTransaction() = default;
    CompositeSaveTransaction(const CompositeSaveTransaction &) = delete;
    CompositeSaveTransaction &operator=(const CompositeSaveTransaction &) = delete;

    ~CompositeSaveTransaction() {
        if (m_owned && !m_preserveRecovery) {
            std::error_code error;
            std::filesystem::remove_all(m_transactionDir, error);
        }
    }

    bool begin(const std::filesystem::path &directory, const std::string &baseName) {
        if (m_owned) {
            m_error = "save transaction has already begun";
            return false;
        }
        if (!isFileName(baseName)) {
            m_error = "document name must be a single nonempty filename";
            return false;
        }
        m_directory = directory;
        m_transactionDir = directory / ("." + baseName + ".saving");
        std::error_code error;
        if (!std::filesystem::create_directory(m_transactionDir, error)) {
            m_error = "could not begin save at '" + m_transactionDir.string() + "': " +
                      (error ? error.message() : "an existing save transaction needs recovery");
            return false;
        }
        m_owned = true;
        std::filesystem::create_directory(m_transactionDir / "staged", error);
        if (!error) {
            std::filesystem::create_directory(m_transactionDir / "backup", error);
        }
        if (error) {
            m_error = "could not prepare save: " + error.message();
            return false;
        }
        return true;
    }

    std::optional<std::filesystem::path> stageFile(const std::string &filename) {
        if (!addFile(filename, true)) {
            return std::nullopt;
        }
        return m_transactionDir / "staged" / filename;
    }

    bool removeFile(const std::string &filename) {
        return addFile(filename, false);
    }

    const std::string &error() const {
        return m_error;
    }

    bool publish() {
        if (!m_owned || m_publishAttempted) {
            m_error = "save transaction is not awaiting publication";
            return false;
        }
        m_publishAttempted = true;
        for (File &file : m_files) {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(m_directory / file.name_, error);
            if (error && error != std::errc::no_such_file_or_directory) {
                m_error =
                    "could not inspect save destination '" + file.name_ + "': " + error.message();
                return false;
            }
            file.hadOriginal_ = std::filesystem::exists(status);
            if (file.hadOriginal_ && !std::filesystem::is_regular_file(status)) {
                m_error = "save destination is not a regular file: '" + file.name_ + "'";
                return false;
            }
        }

        for (File &file : m_files) {
            std::error_code error;
            if (file.hadOriginal_) {
                std::filesystem::rename(
                    m_directory / file.name_,
                    m_transactionDir / "backup" / file.name_,
                    error
                );
                if (error) {
                    return failPublish(file.name_, error);
                }
                file.backedUp_ = true;
            }
            if (file.replace_) {
                std::filesystem::rename(
                    m_transactionDir / "staged" / file.name_,
                    m_directory / file.name_,
                    error
                );
                if (error) {
                    return failPublish(file.name_, error);
                }
                file.published_ = true;
            }
        }
        m_published = true;
        return true;
    }

    // Optional inspection summaries never decide whether canonical data saved.
    // A failed replacement restores the old summary when one existed.
    bool publishOptionalFile(const std::string &filename) {
        if (!m_published || !isFileName(filename) ||
            std::any_of(m_files.begin(), m_files.end(), [&](const File &file) {
                return file.name_ == filename;
            })) {
            m_error = "optional artifact is not distinct from a published document";
            return false;
        }
        const auto destination = m_directory / filename;
        const auto backup = m_transactionDir / "backup" / filename;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(destination, error);
        if ((error && error != std::errc::no_such_file_or_directory) ||
            (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status))) {
            m_error = "optional artifact destination is not an accessible regular file: '" +
                      filename + "'";
            return false;
        }
        const bool hadOriginal = std::filesystem::exists(status);
        if (hadOriginal) {
            std::filesystem::rename(destination, backup, error);
            if (error) {
                m_error =
                    "could not back up optional artifact '" + filename + "': " + error.message();
                return false;
            }
        }
        std::filesystem::rename(m_transactionDir / "staged" / filename, destination, error);
        if (!error) {
            return true;
        }
        m_error = "could not publish optional artifact '" + filename + "': " + error.message();
        if (hadOriginal) {
            std::filesystem::rename(backup, destination, error);
            if (error) {
                m_preserveRecovery = true;
                m_error += "; recovery files retained at '" + m_transactionDir.string() +
                           "': " + error.message();
            }
        }
        return false;
    }

  private:
    struct File {
        std::string name_;
        bool replace_ = true;
        bool hadOriginal_ = false;
        bool backedUp_ = false;
        bool published_ = false;
    };

    static bool isFileName(const std::string &name) {
        return !name.empty() && name != "." && name != ".." && name.back() != '.' &&
               name.back() != ' ' && name.find_first_of("/\\:") == std::string::npos &&
               name.find('\0') == std::string::npos;
    }

    bool addFile(const std::string &filename, bool replace) {
        if (!m_owned || m_publishAttempted) {
            m_error = "save transaction is not accepting staged artifacts";
            return false;
        }
        if (!isFileName(filename)) {
            m_error = "save artifact name must be a single nonempty filename";
            return false;
        }
        if (std::any_of(m_files.begin(), m_files.end(), [&](const File &file) {
                return file.name_ == filename;
            })) {
            m_error = "duplicate save artifact '" + filename + "'";
            return false;
        }
        m_files.push_back(File{filename, replace});
        return true;
    }

    bool failPublish(const std::string &filename, const std::error_code &failure) {
        m_error = "could not publish '" + filename + "': " + failure.message();
        std::string rollbackErrors;
        for (auto file = m_files.rbegin(); file != m_files.rend(); ++file) {
            std::error_code error;
            if (file->published_) {
                std::filesystem::remove(m_directory / file->name_, error);
                if (error) {
                    rollbackErrors += " " + file->name_ + ": " + error.message();
                }
            }
            if (file->backedUp_) {
                std::filesystem::rename(
                    m_transactionDir / "backup" / file->name_,
                    m_directory / file->name_,
                    error
                );
                if (error) {
                    rollbackErrors += " " + file->name_ + ": " + error.message();
                }
            }
        }
        if (!rollbackErrors.empty()) {
            m_preserveRecovery = true;
            m_error += "; rollback incomplete, recovery files retained at '" +
                       m_transactionDir.string() + "':" + rollbackErrors;
        }
        return false;
    }

    std::filesystem::path m_directory;
    std::filesystem::path m_transactionDir;
    std::vector<File> m_files;
    std::string m_error;
    bool m_owned = false;
    bool m_publishAttempted = false;
    bool m_published = false;
    bool m_preserveRecovery = false;
};

} // namespace IRVoxelEditor

#endif /* COMPOSITE_SAVE_TRANSACTION_H */
