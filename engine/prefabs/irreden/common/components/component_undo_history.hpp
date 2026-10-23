#ifndef COMPONENT_UNDO_HISTORY_H
#define COMPONENT_UNDO_HISTORY_H

#include <irreden/time/ir_time_types.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace IRComponents {

struct C_UndoHistory {
    using ContextId = std::uint32_t;
    static constexpr ContextId kDefaultContext = 0;

    struct Record {
        std::string label_;
        std::function<void()> undo_;
        std::function<void()> redo_;
        std::string coalesceKey_;
    };

    struct Labels {
        std::string undo_;
        std::string redo_;
    };

    struct TimedRecord {
        Record record_;
        IRTime::TimePoint pushedAt_;
    };

    struct Context {
        std::vector<TimedRecord> records_;
        std::size_t cursor_ = 0;
    };

    std::size_t depth_ = 256;
    double coalesceWindowSec_ = 0.5;

    C_UndoHistory() {
        contexts_.emplace(kDefaultContext, Context{});
    }

    bool push(Record record, IRTime::TimePoint now) {
        if (dispatching_) {
            return false;
        }

        Context &state = active();
        const bool wasAtTip = state.cursor_ == state.records_.size();
        state.records_.erase(
            state.records_.begin() + static_cast<std::ptrdiff_t>(state.cursor_),
            state.records_.end()
        );

        if (wasAtTip && !record.coalesceKey_.empty() && !state.records_.empty()) {
            TimedRecord &top = state.records_.back();
            const IRTime::SecondsDuration elapsed = now - top.pushedAt_;
            if (top.record_.coalesceKey_ == record.coalesceKey_ &&
                elapsed.count() <= coalesceWindowSec_) {
                top.record_.label_ = std::move(record.label_);
                top.record_.redo_ = std::move(record.redo_);
                top.pushedAt_ = now;
                state.cursor_ = state.records_.size();
                return true;
            }
        }

        state.records_.push_back({std::move(record), now});
        if (state.records_.size() > depth_) {
            const std::size_t excess = state.records_.size() - depth_;
            state.records_.erase(
                state.records_.begin(),
                state.records_.begin() + static_cast<std::ptrdiff_t>(excess)
            );
        }
        state.cursor_ = state.records_.size();
        return true;
    }

    bool undo() {
        Context &state = active();
        if (dispatching_ || state.cursor_ == 0) {
            return false;
        }
        dispatch(state.records_[state.cursor_ - 1].record_.undo_);
        --state.cursor_;
        return true;
    }

    bool redo() {
        Context &state = active();
        if (dispatching_ || state.cursor_ == state.records_.size()) {
            return false;
        }
        dispatch(state.records_[state.cursor_].record_.redo_);
        ++state.cursor_;
        return true;
    }

    bool canUndo() const {
        return active().cursor_ > 0;
    }

    bool canRedo() const {
        const Context &state = active();
        return state.cursor_ < state.records_.size();
    }

    Labels labels() const {
        const Context &state = active();
        Labels result;
        if (state.cursor_ > 0) {
            result.undo_ = state.records_[state.cursor_ - 1].record_.label_;
        }
        if (state.cursor_ < state.records_.size()) {
            result.redo_ = state.records_[state.cursor_].record_.label_;
        }
        return result;
    }

    ContextId createContext() {
        const ContextId id = nextContextId_++;
        contexts_.emplace(id, Context{});
        return id;
    }

    bool selectContext(ContextId id) {
        if (dispatching_ || !contexts_.contains(id)) {
            return false;
        }
        activeContext_ = id;
        return true;
    }

    bool eraseContext(ContextId id) {
        if (dispatching_ || id == kDefaultContext || !contexts_.contains(id)) {
            return false;
        }
        contexts_.erase(id);
        if (activeContext_ == id) {
            activeContext_ = kDefaultContext;
        }
        return true;
    }

    bool clear() {
        if (dispatching_) {
            return false;
        }
        contexts_.clear();
        contexts_.emplace(kDefaultContext, Context{});
        activeContext_ = kDefaultContext;
        nextContextId_ = 1;
        return true;
    }

  private:
    std::unordered_map<ContextId, Context> contexts_;
    ContextId activeContext_ = kDefaultContext;
    ContextId nextContextId_ = 1;
    bool dispatching_ = false;

    Context &active() {
        return contexts_.at(activeContext_);
    }

    const Context &active() const {
        return contexts_.at(activeContext_);
    }

    void dispatch(const std::function<void()> &fn) {
        dispatching_ = true;
        try {
            fn();
        } catch (...) {
            dispatching_ = false;
            throw;
        }
        dispatching_ = false;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_UNDO_HISTORY_H */
