#ifndef DOCUMENT_LIFECYCLE_H
#define DOCUMENT_LIFECYCLE_H

#include <string>
#include <utility>

namespace IRVoxelEditor {

struct DocumentLocation {
    std::string directory_;
    std::string baseName_;

    bool operator==(const DocumentLocation &) const = default;

    bool valid() const {
        return !directory_.empty() && directory_.find('\0') == std::string::npos &&
               !baseName_.empty() && baseName_ != "." && baseName_ != ".." &&
               baseName_.back() != '.' && baseName_.back() != ' ' &&
               baseName_.find_first_of("/\\:") == std::string::npos &&
               baseName_.find('\0') == std::string::npos;
    }
};

enum class DocumentAction { NONE, NEW_DOCUMENT, OPEN, RELOAD, CLOSE };

// Dirty state is conservative: undo is still an edit, and only successful
// persistence or document replacement establishes a clean checkpoint.
class DocumentLifecycle {
  public:
    const DocumentLocation &location() const {
        return m_location;
    }
    bool dirty() const {
        return m_dirty;
    }
    void markEdited() {
        m_dirty = true;
        m_pendingReady = false;
    }

    bool request(DocumentAction action, DocumentLocation target = {}) {
        m_pendingAction = action;
        m_pendingLocation = std::move(target);
        m_pendingReady = !m_dirty && action != DocumentAction::NONE;
        m_error.clear();
        return m_pendingReady;
    }

    DocumentAction pendingAction() const {
        return m_pendingAction;
    }
    const DocumentLocation &pendingLocation() const {
        return m_pendingLocation;
    }
    bool pendingReady() const {
        return m_pendingReady;
    }

    void discardPendingChanges() {
        m_pendingReady = m_pendingAction != DocumentAction::NONE;
    }

    void cancelPending() {
        m_pendingAction = DocumentAction::NONE;
        m_pendingLocation = {};
        m_pendingReady = false;
        m_error.clear();
    }

    // Call only after the complete canonical bundle has been published.
    void saved(DocumentLocation location) {
        m_location = std::move(location);
        m_dirty = false;
        m_pendingReady = m_pendingAction != DocumentAction::NONE;
        m_error.clear();
    }

    // Call after staged data has successfully replaced the live document.
    void opened(DocumentLocation location) {
        m_location = std::move(location);
        m_dirty = false;
        cancelPending();
    }

    void created(DocumentLocation location = {}) {
        opened(std::move(location));
    }

    void failed(std::string message) {
        m_error = std::move(message);
        m_pendingReady = false;
    }

    const std::string &error() const {
        return m_error;
    }

  private:
    DocumentLocation m_location;
    DocumentLocation m_pendingLocation;
    DocumentAction m_pendingAction = DocumentAction::NONE;
    std::string m_error;
    bool m_dirty = false;
    bool m_pendingReady = false;
};

} // namespace IRVoxelEditor

#endif
